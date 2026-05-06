#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "status/status_types.h"

#define WM_APP_STATUS_PROVIDER_EVENT (WM_APP + 1)
#define WM_APP_STATUS_CHANGED        (WM_APP + 2)

enum StatusProviderEventSource : WPARAM
{
    STATUS_PROVIDER_EVENT_DEFAULT = 0,
    STATUS_PROVIDER_EVENT_WIFI    = 1,
    STATUS_PROVIDER_EVENT_MEDIA   = 2,
    STATUS_PROVIDER_EVENT_MEDIA_READY = 3,
};

bool StatusMonitor_Init(HWND hwnd);
void StatusMonitor_Shutdown();
void StatusMonitor_RefreshAll();
void StatusMonitor_RefreshNonWifi();
void StatusMonitor_RefreshWifiAsync();
void StatusMonitor_RefreshMedia(bool requestRefresh = true);
StatusSnapshot StatusMonitor_GetSnapshot();

// Volume control forwarded to AudioStatusProvider
bool StatusMonitor_SetVolume(float level); // [0.0, 1.0]
bool StatusMonitor_SetMute(bool mute);

// Audio panel interactions
bool StatusMonitor_SetSessionVolume(DWORD processId, float level);
bool StatusMonitor_SetSessionMute(DWORD processId, bool mute);
bool StatusMonitor_SetDefaultAudioEndpoint(const std::wstring& endpointId);

void StatusMonitor_ToggleWifi();
void StatusMonitor_ToggleBluetooth();
void StatusMonitor_ScanWifi();
void StatusMonitor_ConnectWifi(const std::wstring& ssid, const std::wstring& profileName);
bool StatusMonitor_ConnectBluetooth(const std::wstring& addressString);
bool StatusMonitor_DisconnectBluetooth(const std::wstring& addressString);

bool StatusMonitor_SelectMediaSession(int index);
bool StatusMonitor_MediaTogglePlayPause();
bool StatusMonitor_MediaSkipPrevious();
bool StatusMonitor_MediaSkipNext();
bool StatusMonitor_MediaSeekTo(long long positionTicks);
