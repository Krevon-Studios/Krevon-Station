#include "status/notification_status.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <mutex>
#include <shobjidl.h>
#include <thread>
#include <wrl/client.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

using Microsoft::WRL::ComPtr;

namespace
{
    struct __declspec(uuid("905A0FEF-BC53-11DF-8C49-001E4FC686DA")) IBufferByteAccess : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE** value) = 0;
    };

    struct NotificationState
    {
        HWND hwnd = nullptr;
        std::mutex mutex;
        std::vector<NotificationInfo> notifications;
        std::atomic_bool initialized = false;
        std::atomic_bool refreshQueued = false;
        bool supported = false;
        winrt::event_token changedToken{};
    };

    NotificationState g_notif;

    std::wstring ToWString(const winrt::hstring& value)
    {
        return std::wstring(value.c_str(), value.size());
    }

    std::wstring FormatLocalTime(winrt::Windows::Foundation::DateTime const& creationTime)
    {
        std::time_t t = winrt::clock::to_time_t(creationTime);
        std::tm local = {};
        localtime_s(&local, &t);

        wchar_t buf[32] = {};
        wcsftime(buf, ARRAYSIZE(buf), L"%I:%M %p", &local);
        std::wstring text = buf;
        if (!text.empty() && text[0] == L'0')
            text.erase(text.begin());
        return text;
    }

    UINT64 ToUnixSeconds(winrt::Windows::Foundation::DateTime const& creationTime)
    {
        return static_cast<UINT64>(winrt::clock::to_time_t(creationTime));
    }

    std::vector<BYTE> ReadLogoBytes(winrt::Windows::ApplicationModel::AppInfo const& appInfo)
    {
        try
        {
            auto logoRef = appInfo.DisplayInfo().GetLogo({ 32.0f, 32.0f });
            if (!logoRef)
                return {};

            auto stream = logoRef.OpenReadAsync().get();
            const auto size64 = stream.Size();
            if (size64 == 0 || size64 > 1024 * 1024)
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

    NotificationInfo ConvertNotification(winrt::Windows::UI::Notifications::UserNotification const& n)
    {
        NotificationInfo info;
        info.id = n.Id();
        info.createdUnix = ToUnixSeconds(n.CreationTime());
        info.timeText = FormatLocalTime(n.CreationTime());

        try
        {
            auto appInfo = n.AppInfo();
            info.appName = ToWString(appInfo.DisplayInfo().DisplayName());
            info.appUserModelId = ToWString(appInfo.AppUserModelId());
            info.iconBytes = ReadLogoBytes(appInfo);
        }
        catch (...)
        {
            info.appName = L"Application";
        }

        if (info.appName.empty())
            info.appName = L"Application";

        try
        {
            using namespace winrt::Windows::UI::Notifications;
            auto binding = n.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
            if (binding)
            {
                auto texts = binding.GetTextElements();
                if (texts.Size() > 0)
                    info.title = ToWString(texts.GetAt(0).Text());

                for (UINT32 i = 1; i < texts.Size(); ++i)
                {
                    std::wstring next = ToWString(texts.GetAt(i).Text());
                    if (next.empty())
                        continue;
                    if (!info.subtext.empty())
                        info.subtext += L"\n";
                    info.subtext += next;
                }
            }
        }
        catch (...)
        {
        }

        if (info.title.empty())
            info.title = info.appName;

        return info;
    }

    void Publish(std::vector<NotificationInfo> notifications)
    {
        std::sort(notifications.begin(), notifications.end(), [](const NotificationInfo& a, const NotificationInfo& b) {
            if (a.createdUnix != b.createdUnix)
                return a.createdUnix > b.createdUnix;
            return a.id > b.id;
        });

        HWND hwnd = nullptr;
        bool changed = false;
        {
            std::scoped_lock lock(g_notif.mutex);
            changed = (notifications != g_notif.notifications);
            g_notif.notifications = std::move(notifications);
            hwnd = g_notif.hwnd;
        }

        if (changed && hwnd)
            PostMessageW(hwnd, WM_APP_NOTIFICATIONS_CHANGED, 0, 0);
    }

    bool IsAllowed(winrt::Windows::UI::Notifications::Management::UserNotificationListener const& listener)
    {
        using namespace winrt::Windows::UI::Notifications::Management;
        return listener.GetAccessStatus() == UserNotificationListenerAccessStatus::Allowed;
    }

    void SyncNow()
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            using namespace winrt::Windows::UI::Notifications;
            using namespace winrt::Windows::UI::Notifications::Management;
            auto listener = UserNotificationListener::Current();
            if (!IsAllowed(listener))
            {
                Publish({});
                return;
            }

            std::vector<NotificationInfo> next;
            auto notifications = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
            next.reserve(notifications.Size());

            for (auto const& n : notifications)
            {
                try
                {
                    next.push_back(ConvertNotification(n));
                }
                catch (...)
                {
                }
            }

            Publish(std::move(next));
        }
        catch (...)
        {
            Publish({});
        }
    }

    void QueueSync()
    {
        if (!g_notif.initialized || !g_notif.supported)
            return;

        bool expected = false;
        if (!g_notif.refreshQueued.compare_exchange_strong(expected, true))
            return;

        std::thread([]() {
            SyncNow();
            g_notif.refreshQueued = false;
        }).detach();
    }
}

bool NotificationStatus_Init(HWND hwnd)
{
    g_notif.hwnd = hwnd;
    g_notif.initialized = true;

    try
    {
        g_notif.supported = winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(
            L"Windows.UI.Notifications.Management.UserNotificationListener");
        if (!g_notif.supported)
            return false;

        using namespace winrt::Windows::UI::Notifications::Management;
        auto listener = UserNotificationListener::Current();
        g_notif.changedToken = listener.NotificationChanged([](auto const&, auto const&) {
            QueueSync();
        });

        NotificationStatus_RequestAccess();
        QueueSync();
        return true;
    }
    catch (...)
    {
        g_notif.supported = false;
        return false;
    }
}

void NotificationStatus_Shutdown()
{
    if (!g_notif.initialized)
        return;

    try
    {
        if (g_notif.supported)
        {
            auto listener = winrt::Windows::UI::Notifications::Management::UserNotificationListener::Current();
            listener.NotificationChanged(g_notif.changedToken);
        }
    }
    catch (...)
    {
    }

    {
        std::scoped_lock lock(g_notif.mutex);
        g_notif.notifications.clear();
        g_notif.hwnd = nullptr;
    }
    g_notif.initialized = false;
    g_notif.supported = false;
}

void NotificationStatus_RequestAccess()
{
    if (!g_notif.initialized || !g_notif.supported)
        return;

    try
    {
        using namespace winrt::Windows::UI::Notifications::Management;
        auto listener = UserNotificationListener::Current();
        auto status = listener.GetAccessStatus();
        if (status == UserNotificationListenerAccessStatus::Allowed)
        {
            QueueSync();
            return;
        }
        if (status != UserNotificationListenerAccessStatus::Unspecified)
            return;

        auto operation = listener.RequestAccessAsync();
        operation.Completed([](auto const& op, auto) {
            try
            {
                if (op.GetResults() == winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus::Allowed)
                    QueueSync();
            }
            catch (...)
            {
            }
        });
    }
    catch (...)
    {
    }
}

void NotificationStatus_RefreshAsync()
{
    QueueSync();
}

std::vector<NotificationInfo> NotificationStatus_GetNotifications()
{
    std::scoped_lock lock(g_notif.mutex);
    return g_notif.notifications;
}

bool NotificationStatus_Remove(UINT32 notificationId)
{
    if (!g_notif.initialized || !g_notif.supported)
        return false;

    try
    {
        auto listener = winrt::Windows::UI::Notifications::Management::UserNotificationListener::Current();
        if (!IsAllowed(listener))
            return false;
        listener.RemoveNotification(notificationId);
        QueueSync();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool NotificationStatus_ClearAll()
{
    if (!g_notif.initialized || !g_notif.supported)
        return false;

    try
    {
        auto listener = winrt::Windows::UI::Notifications::Management::UserNotificationListener::Current();
        if (!IsAllowed(listener))
            return false;
        listener.ClearNotifications();
        QueueSync();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool NotificationStatus_Activate(const NotificationInfo& notification)
{
    if (notification.appUserModelId.empty())
        return false;

    try
    {
        ComPtr<IApplicationActivationManager> activator;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&activator));
        if (FAILED(hr) || !activator)
            return false;

        DWORD pid = 0;
        hr = activator->ActivateApplication(notification.appUserModelId.c_str(), nullptr, AO_NONE, &pid);
        return SUCCEEDED(hr);
    }
    catch (...)
    {
        return false;
    }
}
