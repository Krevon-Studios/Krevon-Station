#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "status/status_types.h"

struct WifiStatusProvider
{
    HWND hwnd = nullptr;
    HANDLE clientHandle = nullptr;
    WifiIconState iconState = WifiIconState::Disconnected;
    bool radioOn = false;
    std::wstring ssid;  // Connected SSID name, empty when disconnected
    std::wstring connectingSsid; // SSID currently being connected to
    ULONGLONG connectingStartTick = 0;
};

bool WifiStatus_Init(WifiStatusProvider& provider, HWND hwnd);
void WifiStatus_Shutdown(WifiStatusProvider& provider);
void WifiStatus_Refresh(WifiStatusProvider& provider);
WifiIconState WifiStatus_GetIconState(const WifiStatusProvider& provider);
bool WifiStatus_IsRadioOn(const WifiStatusProvider& provider);
void WifiStatus_Toggle(WifiStatusProvider& provider);
void WifiStatus_Scan(WifiStatusProvider& provider);
void WifiStatus_Connect(WifiStatusProvider& provider, const std::wstring& ssid, const std::wstring& profileName);

void WifiStatus_PopulateNetworks(WifiStatusProvider& provider, std::vector<WifiNetwork>& networks);
