#include "status/media_status.h"
#include "status/status_monitor.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unknwn.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

using MediaWinrtSession = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using MediaWinrtSessionManager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using MediaWinrtPlaybackStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

struct MediaSessionHook
{
    MediaWinrtSession session{ nullptr };
    winrt::event_token mediaToken{};
    winrt::event_token playbackToken{};
    winrt::event_token timelineToken{};
};

enum class MediaWorkKind
{
    Refresh,
    TogglePlayPause,
    SkipPrevious,
    SkipNext,
    Seek,
};

struct MediaWorkItem
{
    MediaWorkKind kind = MediaWorkKind::Refresh;
    long long positionTicks = 0;
};

struct MediaStatusProviderImpl
{
    HWND hwnd = nullptr;
    std::mutex mutex;
    std::atomic_bool initialized = false;
    bool supported = false;
    MediaWinrtSessionManager manager{ nullptr };
    winrt::event_token sessionsToken{};
    winrt::event_token currentToken{};
    std::vector<MediaSessionHook> hooks;
    std::vector<MediaWinrtSession> sessions;
    std::vector<MediaSessionInfo> cachedInfos;
    int currentIndex = -1;
    int selectedIndex = -1;
    std::wstring selectedSourceId;

    std::thread worker;
    std::mutex workerMutex;
    std::condition_variable workerCv;
    std::deque<MediaWorkItem> workQueue;
    bool workerStop = false;
    bool refreshQueued = false;
};

namespace
{
    struct __declspec(uuid("905A0FEF-BC53-11DF-8C49-001E4FC686DA")) IBufferByteAccess : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE** value) = 0;
    };

    std::wstring ToWString(winrt::hstring const& value)
    {
        return std::wstring(value.c_str(), value.size());
    }

    void Notify(HWND hwnd)
    {
        if (hwnd)
            PostMessageW(hwnd, WM_APP_STATUS_PROVIDER_EVENT, STATUS_PROVIDER_EVENT_MEDIA, 0);
    }

    void NotifyReady(HWND hwnd)
    {
        if (hwnd)
            PostMessageW(hwnd, WM_APP_STATUS_PROVIDER_EVENT, STATUS_PROVIDER_EVENT_MEDIA_READY, 0);
    }

    MediaPlaybackState ConvertPlaybackState(MediaWinrtPlaybackStatus status)
    {
        switch (status)
        {
        case MediaWinrtPlaybackStatus::Playing: return MediaPlaybackState::Playing;
        case MediaWinrtPlaybackStatus::Paused:  return MediaPlaybackState::Paused;
        case MediaWinrtPlaybackStatus::Stopped: return MediaPlaybackState::Stopped;
        default:                      return MediaPlaybackState::None;
        }
    }

    std::vector<BYTE> ReadThumbnailBytes(winrt::Windows::Storage::Streams::IRandomAccessStreamReference const& thumbnail)
    {
        if (!thumbnail)
            return {};

        try
        {
            auto stream = thumbnail.OpenReadAsync().get();
            const auto size64 = stream.Size();
            if (size64 == 0 || size64 > 4ull * 1024ull * 1024ull)
                return {};

            winrt::Windows::Storage::Streams::Buffer buffer(static_cast<UINT32>(size64));
            auto readBuffer = stream.ReadAsync(buffer, buffer.Capacity(), winrt::Windows::Storage::Streams::InputStreamOptions::None).get();

            BYTE* raw = nullptr;
            auto access = readBuffer.as<IBufferByteAccess>();
            if (FAILED(access->Buffer(&raw)) || !raw)
                return {};

            return std::vector<BYTE>(raw, raw + readBuffer.Length());
        }
        catch (...)
        {
            return {};
        }
    }

    std::wstring FriendlyAppName(winrt::hstring const& sourceAppUserModelId)
    {
        try
        {
            auto appInfo = winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(sourceAppUserModelId);
            if (appInfo)
            {
                const auto displayName = ToWString(appInfo.DisplayInfo().DisplayName());
                if (!displayName.empty())
                    return displayName;
            }
        }
        catch (...)
        {
        }

        std::wstring fallback = ToWString(sourceAppUserModelId);
        const size_t bang = fallback.find(L'!');
        if (bang != std::wstring::npos)
            fallback.resize(bang);
        const size_t dot = fallback.rfind(L'.');
        if (dot != std::wstring::npos && dot + 1 < fallback.size())
            fallback = fallback.substr(dot + 1);
        return fallback.empty() ? L"Media" : fallback;
    }

    bool SameSession(MediaWinrtSession const& a, MediaWinrtSession const& b)
    {
        if (!a || !b)
            return false;
        try
        {
            return a.SourceAppUserModelId() == b.SourceAppUserModelId();
        }
        catch (...)
        {
            return false;
        }
    }

    void ClearHooks(MediaStatusProviderImpl& impl)
    {
        for (auto& hook : impl.hooks)
        {
            try { if (hook.session) hook.session.MediaPropertiesChanged(hook.mediaToken); } catch (...) {}
            try { if (hook.session) hook.session.PlaybackInfoChanged(hook.playbackToken); } catch (...) {}
            try { if (hook.session) hook.session.TimelinePropertiesChanged(hook.timelineToken); } catch (...) {}
        }
        impl.hooks.clear();
    }

    void RebuildHooks(MediaStatusProviderImpl& impl)
    {
        ClearHooks(impl);
        if (!impl.manager)
            return;

        const HWND hwnd = impl.hwnd;
        for (auto const& session : impl.sessions)
        {
            try
            {
                MediaSessionHook hook;
                hook.session = session;
                hook.mediaToken = session.MediaPropertiesChanged([hwnd](auto const&, auto const&) { Notify(hwnd); });
                hook.playbackToken = session.PlaybackInfoChanged([hwnd](auto const&, auto const&) { Notify(hwnd); });
                hook.timelineToken = session.TimelinePropertiesChanged([hwnd](auto const&, auto const&) { Notify(hwnd); });
                impl.hooks.push_back(std::move(hook));
            }
            catch (...)
            {
            }
        }
    }

    int FindCurrentIndex(MediaWinrtSessionManager const& manager, std::vector<MediaWinrtSession> const& sessions)
    {
        try
        {
            auto current = manager.GetCurrentSession();
            if (!current)
                return -1;

            for (size_t i = 0; i < sessions.size(); ++i)
            {
                if (SameSession(current, sessions[i]))
                    return static_cast<int>(i);
            }
        }
        catch (...)
        {
        }
        return -1;
    }

    int ClampSelectedIndex(MediaStatusProviderImpl& impl)
    {
        if (!impl.selectedSourceId.empty())
        {
            for (size_t i = 0; i < impl.sessions.size(); ++i)
            {
                try
                {
                    if (ToWString(impl.sessions[i].SourceAppUserModelId()) == impl.selectedSourceId)
                        return static_cast<int>(i);
                }
                catch (...)
                {
                }
            }
            impl.selectedSourceId.clear();
        }

        if (impl.selectedIndex >= 0 && impl.selectedIndex < static_cast<int>(impl.sessions.size()))
            return impl.selectedIndex;

        return impl.currentIndex >= 0 ? impl.currentIndex : (impl.sessions.empty() ? -1 : 0);
    }

    MediaSessionInfo ConvertSession(MediaWinrtSession const& session, int index, bool isCurrent)
    {
        MediaSessionInfo info;
        const auto source = session.SourceAppUserModelId();
        info.sourceAppUserModelId = ToWString(source);
        info.sessionId = info.sourceAppUserModelId + L"#" + std::to_wstring(index);
        info.appName = FriendlyAppName(source);
        info.isCurrent = isCurrent;

        try
        {
            const auto playbackInfo = session.GetPlaybackInfo();
            info.playbackState = ConvertPlaybackState(playbackInfo.PlaybackStatus());
            const auto controls = playbackInfo.Controls();
            info.canPlayPause = controls.IsPlayEnabled() || controls.IsPauseEnabled() || controls.IsPlayPauseToggleEnabled();
            info.canSkipPrevious = controls.IsPreviousEnabled();
            info.canSkipNext = controls.IsNextEnabled();
        }
        catch (...)
        {
            info.playbackState = MediaPlaybackState::None;
        }

        try
        {
            const auto timeline = session.GetTimelineProperties();
            info.timelineStartTicks = timeline.StartTime().count();
            info.timelineEndTicks = timeline.EndTime().count();
            info.timelinePositionTicks = timeline.Position().count();
            info.timelineSnapshotMs = GetTickCount64();
            info.hasTimeline = info.timelineEndTicks > info.timelineStartTicks;
        }
        catch (...)
        {
            info.hasTimeline = false;
        }

        try
        {
            const auto props = session.TryGetMediaPropertiesAsync().get();
            info.title = ToWString(props.Title());
            info.subtitle = ToWString(props.Artist());
            if (info.subtitle.empty())
                info.subtitle = ToWString(props.Subtitle());
            if (info.subtitle.empty())
                info.subtitle = ToWString(props.AlbumTitle());
            info.thumbnailBytes = ReadThumbnailBytes(props.Thumbnail());
        }
        catch (...)
        {
        }

        if (info.title.empty())
            info.title = info.appName.empty() ? L"Media" : info.appName;

        return info;
    }

    MediaWinrtSession ActiveSessionLocked(MediaStatusProviderImpl& impl)
    {
        const int index = ClampSelectedIndex(impl);
        if (index < 0 || index >= static_cast<int>(impl.sessions.size()))
        return nullptr;
        return impl.sessions[index];
    }

    void RefreshCacheOnWorker(MediaStatusProviderImpl& impl)
    {
        std::scoped_lock lock(impl.mutex);
        impl.cachedInfos.clear();
        impl.sessions.clear();
        impl.currentIndex = -1;
        impl.selectedIndex = -1;

        if (!impl.manager)
        {
            ClearHooks(impl);
            return;
        }

        try
        {
            auto sessions = impl.manager.GetSessions();
            impl.sessions.reserve(sessions.Size());
            for (auto const& session : sessions)
                impl.sessions.push_back(session);

            impl.currentIndex = FindCurrentIndex(impl.manager, impl.sessions);
            impl.selectedIndex = ClampSelectedIndex(impl);

            impl.cachedInfos.reserve(impl.sessions.size());
            for (size_t i = 0; i < impl.sessions.size(); ++i)
                impl.cachedInfos.push_back(ConvertSession(impl.sessions[i], static_cast<int>(i), static_cast<int>(i) == impl.currentIndex));

            RebuildHooks(impl);
        }
        catch (...)
        {
            impl.sessions.clear();
            impl.cachedInfos.clear();
            impl.currentIndex = -1;
            impl.selectedIndex = -1;
            ClearHooks(impl);
        }
    }

    void QueueWork(MediaStatusProviderImpl* impl, MediaWorkKind kind, long long positionTicks = 0)
    {
        if (!impl || !impl->initialized)
            return;

        {
            std::scoped_lock lock(impl->workerMutex);
            if (impl->workerStop)
                return;
            if (kind == MediaWorkKind::Refresh)
            {
                if (impl->refreshQueued)
                    return;
                impl->refreshQueued = true;
            }
            impl->workQueue.push_back({ kind, positionTicks });
        }
        impl->workerCv.notify_one();
    }

    void ExecuteControlOnWorker(MediaStatusProviderImpl& impl, const MediaWorkItem& item)
    {
        MediaWinrtSession session{ nullptr };
        HWND hwnd = nullptr;
        {
            std::scoped_lock lock(impl.mutex);
            session = ActiveSessionLocked(impl);
            hwnd = impl.hwnd;
        }

        if (!session)
            return;

        try
        {
            switch (item.kind)
            {
            case MediaWorkKind::TogglePlayPause:
                session.TryTogglePlayPauseAsync().get();
                break;
            case MediaWorkKind::SkipPrevious:
                session.TrySkipPreviousAsync().get();
                break;
            case MediaWorkKind::SkipNext:
                session.TrySkipNextAsync().get();
                break;
            case MediaWorkKind::Seek:
                session.TryChangePlaybackPositionAsync(item.positionTicks).get();
                break;
            default:
                break;
            }
        }
        catch (...)
        {
        }

        QueueWork(&impl, MediaWorkKind::Refresh);
        if (item.kind != MediaWorkKind::Seek)
            Notify(hwnd);
    }

    void WorkerLoop(MediaStatusProviderImpl* impl)
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            impl->supported = winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(
                L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager");
            if (impl->supported && impl->initialized)
            {
                auto manager = MediaWinrtSessionManager::RequestAsync().get();
                if (manager && impl->initialized)
                {
                    std::scoped_lock lock(impl->mutex);
                    impl->manager = manager;
                    const HWND hwnd = impl->hwnd;
                    impl->sessionsToken = impl->manager.SessionsChanged([hwnd](auto const&, auto const&) { Notify(hwnd); });
                    impl->currentToken = impl->manager.CurrentSessionChanged([hwnd](auto const&, auto const&) { Notify(hwnd); });
                }
            }
        }
        catch (...)
        {
            impl->supported = false;
        }

        QueueWork(impl, MediaWorkKind::Refresh);

        for (;;)
        {
            MediaWorkItem item;
            {
                std::unique_lock lock(impl->workerMutex);
                impl->workerCv.wait(lock, [&]() { return impl->workerStop || !impl->workQueue.empty(); });
                if (impl->workerStop)
                    return;
                item = impl->workQueue.front();
                impl->workQueue.pop_front();
                if (item.kind == MediaWorkKind::Refresh)
                    impl->refreshQueued = false;
            }

            if (item.kind == MediaWorkKind::Refresh)
            {
                RefreshCacheOnWorker(*impl);
                NotifyReady(impl->hwnd);
            }
            else
            {
                ExecuteControlOnWorker(*impl, item);
            }
        }
    }
}

bool MediaStatus_Init(MediaStatusProvider& provider, HWND hwnd)
{
    if (!provider.impl)
        provider.impl = new MediaStatusProviderImpl();

    provider.impl->hwnd = hwnd;
    provider.impl->initialized = true;
    provider.impl->worker = std::thread(WorkerLoop, provider.impl);
    return true;
}

void MediaStatus_Shutdown(MediaStatusProvider& provider)
{
    if (!provider.impl)
        return;

    provider.impl->initialized = false;

    {
        std::scoped_lock lock(provider.impl->workerMutex);
        provider.impl->workerStop = true;
        provider.impl->workQueue.clear();
        provider.impl->refreshQueued = false;
    }
    provider.impl->workerCv.notify_one();
    if (provider.impl->worker.joinable())
        provider.impl->worker.join();

    {
        std::scoped_lock lock(provider.impl->mutex);
        ClearHooks(*provider.impl);
        try { if (provider.impl->manager) provider.impl->manager.SessionsChanged(provider.impl->sessionsToken); } catch (...) {}
        try { if (provider.impl->manager) provider.impl->manager.CurrentSessionChanged(provider.impl->currentToken); } catch (...) {}
        provider.impl->manager = nullptr;
        provider.impl->sessions.clear();
        provider.impl->cachedInfos.clear();
        provider.impl->currentIndex = -1;
        provider.impl->selectedIndex = -1;
        provider.impl->selectedSourceId.clear();
        provider.impl->hwnd = nullptr;
    }

    delete provider.impl;
    provider.impl = nullptr;
}

void MediaStatus_RequestRefresh(MediaStatusProvider& provider)
{
    QueueWork(provider.impl, MediaWorkKind::Refresh);
}

void MediaStatus_GetSessions(const MediaStatusProvider& provider, std::vector<MediaSessionInfo>& outSessions, int& currentIndex, int& selectedIndex)
{
    outSessions.clear();
    currentIndex = -1;
    selectedIndex = -1;
    if (!provider.impl)
        return;

    std::scoped_lock lock(provider.impl->mutex);
    outSessions = provider.impl->cachedInfos;
    currentIndex = provider.impl->currentIndex;
    selectedIndex = provider.impl->selectedIndex;
}

bool MediaStatus_SelectSession(MediaStatusProvider& provider, int index)
{
    if (!provider.impl)
        return false;

    {
        std::scoped_lock lock(provider.impl->mutex);
        if (index < 0 || index >= static_cast<int>(provider.impl->sessions.size()))
            return false;

        provider.impl->selectedIndex = index;
        try
        {
            provider.impl->selectedSourceId = ToWString(provider.impl->sessions[index].SourceAppUserModelId());
        }
        catch (...)
        {
            provider.impl->selectedSourceId.clear();
        }
    }

    Notify(provider.impl->hwnd);
    return true;
}

bool MediaStatus_TogglePlayPause(MediaStatusProvider& provider)
{
    QueueWork(provider.impl, MediaWorkKind::TogglePlayPause);
    return provider.impl != nullptr;
}

bool MediaStatus_SkipPrevious(MediaStatusProvider& provider)
{
    QueueWork(provider.impl, MediaWorkKind::SkipPrevious);
    return provider.impl != nullptr;
}

bool MediaStatus_SkipNext(MediaStatusProvider& provider)
{
    QueueWork(provider.impl, MediaWorkKind::SkipNext);
    return provider.impl != nullptr;
}

bool MediaStatus_SeekTo(MediaStatusProvider& provider, long long positionTicks)
{
    QueueWork(provider.impl, MediaWorkKind::Seek, positionTicks);
    return provider.impl != nullptr;
}
