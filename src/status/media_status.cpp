#include "status/media_status.h"
#include "status/status_monitor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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
    std::wstring selectedSessionId;

    std::thread worker;
    std::mutex workerMutex;
    std::condition_variable workerCv;
    std::deque<MediaWorkItem> workQueue;
    bool workerStop = false;
    bool refreshQueued = false;
    int followUpRefreshes = 0;
    std::chrono::steady_clock::time_point nextFollowUpRefresh = {};
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

    ULONGLONG TimelineSnapshotTickFromLastUpdated(winrt::Windows::Foundation::DateTime const& lastUpdated)
    {
        try
        {
            const auto now = winrt::clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdated).count();
            if (elapsed < 0)
                elapsed = 0;

            const ULONGLONG nowTick = GetTickCount64();
            const ULONGLONG elapsedTick = static_cast<ULONGLONG>(elapsed);
            return elapsedTick >= nowTick ? nowTick : nowTick - elapsedTick;
        }
        catch (...)
        {
            return GetTickCount64();
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
        // Packaged (UWP/MSIX) apps: Windows gives us the display name directly.
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

        std::wstring id = ToWString(sourceAppUserModelId);

        // Strip "Package!AppId" suffix used by packaged apps.
        const size_t bang = id.find(L'!');
        if (bang != std::wstring::npos)
            id.resize(bang);

        // Extract just the filename for path-based AUMIDs ("C:\...\zen.exe" → "zen.exe").
        const size_t lastSep = id.find_last_of(L"\\/");
        std::wstring name = (lastSep != std::wstring::npos) ? id.substr(lastSep + 1) : id;

        // Strip .exe extension.
        if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0)
            name.resize(name.size() - 4);

        // Known app table — checked case-insensitively against the cleaned name
        // and against the full id (catches "com.vendor.app" style AUMIDs).
        static const struct { const wchar_t* match; const wchar_t* friendly; } kApps[] =
        {
            { L"zen",                           L"Zen Browser"          },
            { L"firefox",                       L"Firefox"              },
            { L"waterfox",                      L"Waterfox"             },
            { L"librewolf",                     L"LibreWolf"            },
            { L"chrome",                        L"Chrome"               },
            { L"msedge",                        L"Edge"                 },
            { L"brave",                         L"Brave"                },
            { L"opera",                         L"Opera"                },
            { L"vivaldi",                       L"Vivaldi"              },
            { L"spotify",                       L"Spotify"              },
            // YouTube Music Desktop App ships various AUMIDs depending on version/install.
            { L"YouTube Music Desktop App",     L"YouTube Music"        },
            { L"com.ytmdesktop.ytmdesktop",     L"YouTube Music"        },
            { L"ytmdesktop",                    L"YouTube Music"        },
            { L"vlc",                           L"VLC"                  },
            { L"wmplayer",                      L"Windows Media Player" },
            { L"winamp",                        L"Winamp"               },
            { L"foobar2000",                    L"foobar2000"           },
            { L"mpc-hc",                        L"MPC-HC"               },
            { L"mpc-hc64",                      L"MPC-HC"               },
            { L"mpc-be",                        L"MPC-BE"               },
            { L"mpc-be64",                      L"MPC-BE"               },
            { L"aimp",                          L"AIMP"                 },
            { L"musicbee",                      L"MusicBee"             },
            { L"tidal",                         L"TIDAL"                },
        };

        for (const auto& entry : kApps)
        {
            if (_wcsicmp(name.c_str(), entry.match) == 0 ||
                _wcsicmp(id.c_str(),  entry.match) == 0)
                return entry.friendly;
        }

        // GUID-style AUMID ({XXXXXXXX-...}) — not a useful display name.
        if (!name.empty() && name[0] == L'{')
            return L"Media";

        // For reverse-DNS AUMIDs ("com.vendor.app"), use the last component.
        const size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos && dot + 1 < name.size())
            name = name.substr(dot + 1);

        // Capitalize first letter.
        if (!name.empty() && name[0] >= L'a' && name[0] <= L'z')
            name[0] = name[0] - L'a' + L'A';

        return name.empty() ? L"Media" : name;
    }

    uintptr_t SessionIdentityValue(MediaWinrtSession const& session)
    {
        if (!session)
            return 0;

        try
        {
            auto unknown = session.as<::IUnknown>();
            return reinterpret_cast<uintptr_t>(unknown.get());
        }
        catch (...)
        {
            return 0;
        }
    }

    std::wstring BuildSessionId(MediaWinrtSession const& session, int index)
    {
        std::wstring sourceId;
        try
        {
            sourceId = ToWString(session.SourceAppUserModelId());
        }
        catch (...)
        {
        }

        const uintptr_t identity = SessionIdentityValue(session);
        if (identity != 0)
            return sourceId + L"#" + std::to_wstring(identity);

        return sourceId + L"#" + std::to_wstring(index);
    }

    bool SameSession(MediaWinrtSession const& a, MediaWinrtSession const& b)
    {
        if (!a || !b)
            return false;

        const uintptr_t aIdentity = SessionIdentityValue(a);
        const uintptr_t bIdentity = SessionIdentityValue(b);
        if (aIdentity != 0 && bIdentity != 0)
            return aIdentity == bIdentity;

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
        if (!impl.selectedSessionId.empty())
        {
            for (size_t i = 0; i < impl.sessions.size(); ++i)
            {
                if (BuildSessionId(impl.sessions[i], static_cast<int>(i)) == impl.selectedSessionId)
                    return static_cast<int>(i);
            }
            impl.selectedSessionId.clear();
        }

        if (impl.selectedIndex >= 0 && impl.selectedIndex < static_cast<int>(impl.sessions.size()))
            return impl.selectedIndex;

        return impl.currentIndex >= 0 ? impl.currentIndex : (impl.sessions.empty() ? -1 : 0);
    }

    bool SameMediaIdentity(const MediaSessionInfo& lhs, const MediaSessionInfo& rhs)
    {
        return lhs.sourceAppUserModelId == rhs.sourceAppUserModelId
            && lhs.title == rhs.title
            && lhs.subtitle == rhs.subtitle;
    }

    MediaSessionInfo ConvertSession(MediaWinrtSession const& session, int index, bool isCurrent)
    {
        MediaSessionInfo info;
        const auto source = session.SourceAppUserModelId();
        info.sourceAppUserModelId = ToWString(source);
        info.sessionId = BuildSessionId(session, index);
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

        try
        {
            const auto timeline = session.GetTimelineProperties();
            info.timelineStartTicks = timeline.StartTime().count();
            info.timelineEndTicks = timeline.EndTime().count();
            info.timelinePositionTicks = timeline.Position().count();
            info.timelineSnapshotMs = TimelineSnapshotTickFromLastUpdated(timeline.LastUpdatedTime());
            info.hasTimeline = info.timelineEndTicks > info.timelineStartTicks;
        }
        catch (...)
        {
            info.hasTimeline = false;
        }

        return info;
    }

    MediaWinrtSession ActiveSessionLocked(MediaStatusProviderImpl& impl)
    {
        const int index = ClampSelectedIndex(impl);
        if (index < 0 || index >= static_cast<int>(impl.sessions.size()))
        return nullptr;
        return impl.sessions[index];
    }

    bool RefreshCacheOnWorker(MediaStatusProviderImpl& impl)
    {
        std::scoped_lock lock(impl.mutex);
        bool needsFollowUp = false;
        const std::vector<MediaSessionInfo> previousInfos = impl.cachedInfos;
        impl.cachedInfos.clear();
        impl.sessions.clear();
        impl.currentIndex = -1;
        impl.selectedIndex = -1;

        if (!impl.manager)
        {
            ClearHooks(impl);
            return false;
        }

        try
        {
            auto sessions = impl.manager.GetSessions();
            impl.sessions.reserve(sessions.Size());
            for (auto const& session : sessions)
                impl.sessions.push_back(session);
            if (impl.sessions.size() != previousInfos.size())
                needsFollowUp = true;

            impl.currentIndex = FindCurrentIndex(impl.manager, impl.sessions);
            impl.selectedIndex = ClampSelectedIndex(impl);

            impl.cachedInfos.reserve(impl.sessions.size());
            for (size_t i = 0; i < impl.sessions.size(); ++i)
            {
                MediaSessionInfo info = ConvertSession(impl.sessions[i], static_cast<int>(i), static_cast<int>(i) == impl.currentIndex);
                auto previousIt = std::find_if(previousInfos.begin(), previousInfos.end(),
                    [&](const MediaSessionInfo& previous) { return previous.sessionId == info.sessionId; });

                if (previousIt == previousInfos.end())
                {
                    info.mediaPropertiesChangedMs = GetTickCount64();
                }
                else if (!SameMediaIdentity(*previousIt, info))
                {
                    info.mediaPropertiesChangedMs = GetTickCount64();
                    needsFollowUp = true;
                }
                else
                {
                    info.mediaPropertiesChangedMs = previousIt->mediaPropertiesChangedMs;
                }

                impl.cachedInfos.push_back(std::move(info));
            }

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
        return needsFollowUp;
    }

    void ScheduleFollowUpRefreshes(MediaStatusProviderImpl& impl)
    {
        std::scoped_lock lock(impl.workerMutex);
        if (impl.workerStop)
            return;

        impl.followUpRefreshes = (std::max)(impl.followUpRefreshes, 8);
        if (impl.nextFollowUpRefresh == std::chrono::steady_clock::time_point{})
            impl.nextFollowUpRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        impl.workerCv.notify_one();
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
            bool hasItem = false;
            {
                std::unique_lock lock(impl->workerMutex);
                for (;;)
                {
                    if (impl->workerStop)
                        return;

                    if (!impl->workQueue.empty())
                    {
                        item = impl->workQueue.front();
                        impl->workQueue.pop_front();
                        if (item.kind == MediaWorkKind::Refresh)
                            impl->refreshQueued = false;
                        hasItem = true;
                        break;
                    }

                    if (impl->followUpRefreshes > 0)
                    {
                        const auto now = std::chrono::steady_clock::now();
                        if (now >= impl->nextFollowUpRefresh)
                        {
                            item.kind = MediaWorkKind::Refresh;
                            --impl->followUpRefreshes;
                            impl->nextFollowUpRefresh = impl->followUpRefreshes > 0
                                ? now + std::chrono::milliseconds(300)
                                : std::chrono::steady_clock::time_point{};
                            hasItem = true;
                            break;
                        }

                        impl->workerCv.wait_until(lock, impl->nextFollowUpRefresh);
                    }
                    else
                    {
                        // Timed wait: poll GSMTC periodically even without events.
                        // Browsers (e.g. Chrome/YouTube) may delay MediaPropertiesChanged
                        // or TimelinePropertiesChanged by several seconds on video switch,
                        // so we must not block indefinitely.
                        if (impl->workerCv.wait_for(lock, std::chrono::seconds(2)) == std::cv_status::timeout
                            && !impl->workerStop)
                        {
                            item.kind = MediaWorkKind::Refresh;
                            hasItem = true;
                            break;
                        }
                    }
                }
            }

            if (!hasItem)
                continue;

            if (item.kind == MediaWorkKind::Refresh)
            {
                const bool needsFollowUp = RefreshCacheOnWorker(*impl);
                if (needsFollowUp)
                    ScheduleFollowUpRefreshes(*impl);
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
        provider.impl->followUpRefreshes = 0;
        provider.impl->nextFollowUpRefresh = {};
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
        provider.impl->selectedSessionId.clear();
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
        provider.impl->selectedSessionId = BuildSessionId(provider.impl->sessions[index], index);
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
