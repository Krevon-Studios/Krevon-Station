#include "status/status_monitor.h"
#include "status/audio_status.h"
#include "status/bluetooth_status.h"
#include "status/media_status.h"
#include "status/wifi_status.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
    enum class WifiWorkKind
    {
        Refresh,
        Toggle,
        Scan,
        Connect,
    };

    struct WifiWorkItem
    {
        WifiWorkKind kind = WifiWorkKind::Refresh;
        std::wstring ssid;
        std::wstring profileName;
        bool startRefreshBurst = false;
    };

    struct StatusMonitorState
    {
        HWND hwnd = nullptr;
        WifiStatusProvider wifi;
        BluetoothStatusProvider bluetooth;
        AudioStatusProvider audio;
        MediaStatusProvider media;
        StatusSnapshot snapshot;
        std::mutex mutex;
        std::atomic_bool initialized = false;

        std::thread wifiWorker;
        std::mutex wifiWorkerMutex;
        std::condition_variable wifiWorkerCv;
        std::deque<WifiWorkItem> wifiQueue;
        bool wifiWorkerStop = false;
        bool wifiRefreshQueued = false;
        bool wifiScanQueued = false;
        int wifiFollowUpRefreshes = 0;
        std::chrono::steady_clock::time_point wifiNextFollowUpRefresh = {};

        std::mutex bluetoothMutex;
    };

    StatusMonitorState g_status;

    void PublishSnapshot(StatusSnapshot next)
    {
        HWND hwnd = nullptr;
        bool changed = false;
        {
            std::scoped_lock lock(g_status.mutex);
            if (!g_status.initialized)
                return;

            changed = next != g_status.snapshot;
            g_status.snapshot = std::move(next);
            hwnd = g_status.hwnd;
        }

        if (changed && hwnd)
            PostMessageW(hwnd, WM_APP_STATUS_CHANGED, 0, 0);
    }

    void SortAudioSessions(std::vector<AudioSession>& sessions)
    {
        std::sort(sessions.begin(), sessions.end(), [](const AudioSession& a, const AudioSession& b) {
            if (a.isSystemSound && !b.isSystemSound) return true;
            if (!a.isSystemSound && b.isSystemSound) return false;
            return a.name < b.name;
        });
    }

    void FillMediaSnapshot(StatusSnapshot& next, bool requestRefresh)
    {
        if (requestRefresh)
            MediaStatus_RequestRefresh(g_status.media);
        MediaStatus_GetSessions(g_status.media, next.mediaSessions, next.mediaCurrentIndex, next.mediaSelectedIndex);
    }

    StatusSnapshot GetSnapshotCopy()
    {
        std::scoped_lock lock(g_status.mutex);
        return g_status.snapshot;
    }

    void RefreshWifiSnapshotOnWorker()
    {
        if (!g_status.initialized)
            return;

        WifiStatus_Refresh(g_status.wifi);

        std::vector<WifiNetwork> networks;
        WifiStatus_PopulateNetworks(g_status.wifi, networks);

        StatusSnapshot next = GetSnapshotCopy();
        next.wifi = WifiStatus_GetIconState(g_status.wifi);
        next.wifiSsid = g_status.wifi.ssid;
        next.wifiRadioOn = WifiStatus_IsRadioOn(g_status.wifi);
        next.wifiNetworks = std::move(networks);

        PublishSnapshot(std::move(next));
    }

    void StartWifiRefreshBurstLocked()
    {
        constexpr int FOLLOW_UP_REFRESH_COUNT = 8;
        g_status.wifiFollowUpRefreshes = (std::max)(g_status.wifiFollowUpRefreshes, FOLLOW_UP_REFRESH_COUNT);
        g_status.wifiNextFollowUpRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }

    void QueueWifiWork(WifiWorkItem item)
    {
        if (!g_status.initialized)
            return;

        {
            std::scoped_lock lock(g_status.wifiWorkerMutex);
            if (g_status.wifiWorkerStop)
                return;

            if (item.kind == WifiWorkKind::Refresh)
            {
                if (g_status.wifiRefreshQueued)
                    return;
                g_status.wifiRefreshQueued = true;
            }
            else if (item.kind == WifiWorkKind::Scan)
            {
                if (g_status.wifiScanQueued)
                    return;
                g_status.wifiScanQueued = true;
            }

            g_status.wifiQueue.push_back(std::move(item));
        }

        g_status.wifiWorkerCv.notify_one();
    }

    void QueueWifiRefreshBurst()
    {
        if (!g_status.initialized)
            return;

        {
            std::scoped_lock lock(g_status.wifiWorkerMutex);
            if (g_status.wifiWorkerStop)
                return;
            StartWifiRefreshBurstLocked();
        }

        g_status.wifiWorkerCv.notify_one();
    }

    void WifiWorkerLoop()
    {
        for (;;)
        {
            WifiWorkItem item;
            bool hasItem = false;

            {
                std::unique_lock lock(g_status.wifiWorkerMutex);
                for (;;)
                {
                    if (g_status.wifiWorkerStop)
                        return;

                    if (!g_status.wifiQueue.empty())
                    {
                        item = std::move(g_status.wifiQueue.front());
                        g_status.wifiQueue.pop_front();

                        if (item.kind == WifiWorkKind::Refresh)
                            g_status.wifiRefreshQueued = false;
                        else if (item.kind == WifiWorkKind::Scan)
                            g_status.wifiScanQueued = false;

                        hasItem = true;
                        break;
                    }

                    if (g_status.wifiFollowUpRefreshes > 0)
                    {
                        const auto now = std::chrono::steady_clock::now();
                        if (now >= g_status.wifiNextFollowUpRefresh)
                        {
                            item.kind = WifiWorkKind::Refresh;
                            item.startRefreshBurst = false;
                            --g_status.wifiFollowUpRefreshes;
                            g_status.wifiNextFollowUpRefresh = now + std::chrono::seconds(1);
                            hasItem = true;
                            break;
                        }

                        g_status.wifiWorkerCv.wait_until(lock, g_status.wifiNextFollowUpRefresh);
                    }
                    else
                    {
                        g_status.wifiWorkerCv.wait(lock);
                    }
                }
            }

            if (!hasItem)
                continue;

            switch (item.kind)
            {
            case WifiWorkKind::Refresh:
                RefreshWifiSnapshotOnWorker();
                break;

            case WifiWorkKind::Toggle:
                WifiStatus_Toggle(g_status.wifi);
                RefreshWifiSnapshotOnWorker();
                break;

            case WifiWorkKind::Scan:
                WifiStatus_Scan(g_status.wifi);
                break;

            case WifiWorkKind::Connect:
                WifiStatus_Connect(g_status.wifi, item.ssid, item.profileName);
                RefreshWifiSnapshotOnWorker();
                break;
            }

            if (item.startRefreshBurst)
                QueueWifiRefreshBurst();
        }
    }

    void StartWifiWorker()
    {
        {
            std::scoped_lock lock(g_status.wifiWorkerMutex);
            g_status.wifiWorkerStop = false;
            g_status.wifiRefreshQueued = false;
            g_status.wifiScanQueued = false;
            g_status.wifiFollowUpRefreshes = 0;
            g_status.wifiQueue.clear();
        }

        g_status.wifiWorker = std::thread(WifiWorkerLoop);
    }

    void StopWifiWorker()
    {
        {
            std::scoped_lock lock(g_status.wifiWorkerMutex);
            g_status.wifiWorkerStop = true;
            g_status.wifiQueue.clear();
            g_status.wifiRefreshQueued = false;
            g_status.wifiScanQueued = false;
            g_status.wifiFollowUpRefreshes = 0;
        }

        g_status.wifiWorkerCv.notify_one();

        if (g_status.wifiWorker.joinable())
            g_status.wifiWorker.join();
    }

    bool QueueBluetoothDeviceOperation(const std::wstring& addressString, bool targetConnected)
    {
        if (!g_status.initialized)
            return false;

        StatusSnapshot next = GetSnapshotCopy();
        {
            std::scoped_lock lock(g_status.bluetoothMutex);
            if (!BluetoothStatus_BeginDeviceOperation(g_status.bluetooth, addressString, targetConnected))
                return false;

            next.bluetooth = BluetoothStatus_GetIconState(g_status.bluetooth);
            next.bluetoothDeviceName = g_status.bluetooth.connectedDeviceName;
            next.bluetoothDevices = g_status.bluetooth.pairedDevices;
        }

        PublishSnapshot(std::move(next));

        HWND hwnd = g_status.hwnd;
        std::thread([addressString, targetConnected, hwnd]() {
            if (targetConnected)
                BluetoothStatus_ConnectDevice(g_status.bluetooth, addressString);
            else
                BluetoothStatus_DisconnectDevice(g_status.bluetooth, addressString);

            if (hwnd)
                PostMessageW(hwnd, WM_APP_STATUS_PROVIDER_EVENT, STATUS_PROVIDER_EVENT_DEFAULT, 0);

            constexpr int FOLLOW_UP_REFRESH_COUNT = 8;
            for (int i = 0; i < FOLLOW_UP_REFRESH_COUNT; ++i)
            {
                Sleep(1000);
                if (hwnd)
                    PostMessageW(hwnd, WM_APP_STATUS_PROVIDER_EVENT, STATUS_PROVIDER_EVENT_DEFAULT, 0);
            }
        }).detach();

        return true;
    }
}

bool StatusMonitor_Init(HWND hwnd)
{
    g_status.hwnd = hwnd;
    g_status.snapshot = {};
    g_status.initialized = true;

    WifiStatus_Init(g_status.wifi, hwnd);
    StartWifiWorker();
    BluetoothStatus_Init(g_status.bluetooth);
    AudioStatus_Init(g_status.audio, hwnd);
    MediaStatus_Init(g_status.media, hwnd);
    StatusMonitor_RefreshAll();
    return true;
}

void StatusMonitor_Shutdown()
{
    if (!g_status.initialized)
        return;

    StopWifiWorker();

    AudioStatus_Shutdown(g_status.audio);
    MediaStatus_Shutdown(g_status.media);
    {
        std::scoped_lock lock(g_status.bluetoothMutex);
        BluetoothStatus_Shutdown(g_status.bluetooth);
    }
    WifiStatus_Shutdown(g_status.wifi);

    std::scoped_lock lock(g_status.mutex);
    g_status.snapshot = {};
    g_status.hwnd = nullptr;
    g_status.initialized = false;
}

void StatusMonitor_RefreshNonWifi()
{
    if (!g_status.initialized)
        return;

    StatusSnapshot next = GetSnapshotCopy();

    {
        std::scoped_lock lock(g_status.bluetoothMutex);
        BluetoothStatus_Refresh(g_status.bluetooth);

        next.bluetooth = BluetoothStatus_GetIconState(g_status.bluetooth);
        next.bluetoothDeviceName = g_status.bluetooth.connectedDeviceName;
        next.bluetoothDevices = g_status.bluetooth.pairedDevices;
    }

    AudioStatus_Refresh(g_status.audio);

    next.volume = AudioStatus_GetIconState(g_status.audio);
    next.volumeLevel = AudioStatus_GetVolumeLevel(g_status.audio);
    next.volumeMuted = AudioStatus_GetMuted(g_status.audio);

    AudioStatus_GetEndpoints(g_status.audio, next.audioEndpoints);
    AudioStatus_GetSessions(g_status.audio, next.audioSessions);
    SortAudioSessions(next.audioSessions);
    FillMediaSnapshot(next, false);

    PublishSnapshot(std::move(next));
}

void StatusMonitor_RefreshMedia(bool requestRefresh)
{
    if (!g_status.initialized)
        return;

    StatusSnapshot next = GetSnapshotCopy();
    FillMediaSnapshot(next, requestRefresh);
    PublishSnapshot(std::move(next));
}

void StatusMonitor_RefreshAll()
{
    StatusMonitor_RefreshNonWifi();
    StatusMonitor_RefreshWifiAsync();
}

void StatusMonitor_RefreshWifiAsync()
{
    QueueWifiWork({ WifiWorkKind::Refresh });
}

StatusSnapshot StatusMonitor_GetSnapshot()
{
    return GetSnapshotCopy();
}

bool StatusMonitor_SetVolume(float level)
{
    if (!g_status.initialized)
        return false;
    return AudioStatus_SetMasterVolume(g_status.audio, level);
}

bool StatusMonitor_SetMute(bool mute)
{
    if (!g_status.initialized)
        return false;
    return AudioStatus_SetMute(g_status.audio, mute);
}

void StatusMonitor_ToggleWifi()
{
    QueueWifiWork({ WifiWorkKind::Toggle, {}, {}, true });
}

void StatusMonitor_ToggleBluetooth()
{
    if (g_status.initialized)
    {
        std::scoped_lock lock(g_status.bluetoothMutex);
        BluetoothStatus_Toggle(g_status.bluetooth);
    }
}

bool StatusMonitor_SetSessionVolume(DWORD processId, float level)
{
    if (!g_status.initialized) return false;
    return AudioStatus_SetSessionVolume(g_status.audio, processId, level);
}

bool StatusMonitor_SetSessionMute(DWORD processId, bool mute)
{
    if (!g_status.initialized) return false;
    return AudioStatus_SetSessionMute(g_status.audio, processId, mute);
}

bool StatusMonitor_SetDefaultAudioEndpoint(const std::wstring& endpointId)
{
    if (!g_status.initialized) return false;
    return AudioStatus_SetDefaultEndpoint(g_status.audio, endpointId);
}

void StatusMonitor_ScanWifi()
{
    QueueWifiWork({ WifiWorkKind::Scan, {}, {}, true });
}

void StatusMonitor_ConnectWifi(const std::wstring& ssid, const std::wstring& profileName)
{
    QueueWifiWork({ WifiWorkKind::Connect, ssid, profileName, true });
}

bool StatusMonitor_ConnectBluetooth(const std::wstring& addressString)
{
    return QueueBluetoothDeviceOperation(addressString, true);
}

bool StatusMonitor_DisconnectBluetooth(const std::wstring& addressString)
{
    return QueueBluetoothDeviceOperation(addressString, false);
}

bool StatusMonitor_SelectMediaSession(int index)
{
    if (!g_status.initialized) return false;
    const bool ok = MediaStatus_SelectSession(g_status.media, index);
    StatusMonitor_RefreshMedia();
    return ok;
}

bool StatusMonitor_MediaTogglePlayPause()
{
    if (!g_status.initialized) return false;
    const bool ok = MediaStatus_TogglePlayPause(g_status.media);
    StatusMonitor_RefreshMedia();
    return ok;
}

bool StatusMonitor_MediaSkipPrevious()
{
    if (!g_status.initialized) return false;
    const bool ok = MediaStatus_SkipPrevious(g_status.media);
    StatusMonitor_RefreshMedia();
    return ok;
}

bool StatusMonitor_MediaSkipNext()
{
    if (!g_status.initialized) return false;
    const bool ok = MediaStatus_SkipNext(g_status.media);
    StatusMonitor_RefreshMedia();
    return ok;
}

bool StatusMonitor_MediaSeekTo(long long positionTicks)
{
    if (!g_status.initialized) return false;
    return MediaStatus_SeekTo(g_status.media, positionTicks);
}
