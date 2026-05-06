#include "status/wifi_status.h"
#include "status/status_monitor.h"
#include <wlanapi.h>
#include <algorithm>
#include <string>

namespace
{
    constexpr DWORD WLAN_CLIENT_VERSION = 2;

    WifiIconState WifiStateFromSignal(ULONG signalQuality)
    {
        if (signalQuality >= 75)
            return WifiIconState::Full;
        if (signalQuality >= 50)
            return WifiIconState::High;
        if (signalQuality >= 25)
            return WifiIconState::Low;
        return WifiIconState::Zero;
    }

    bool QueryInterfaceRadioOn(HANDLE clientHandle, const GUID& interfaceGuid)
    {
        DWORD dataSize = 0;
        PVOID data = nullptr;
        bool radioOn = false;

        if (WlanQueryInterface(clientHandle, &interfaceGuid, wlan_intf_opcode_radio_state, nullptr, &dataSize, &data, nullptr) == ERROR_SUCCESS && data)
        {
            auto* radioState = static_cast<WLAN_RADIO_STATE*>(data);
            for (DWORD i = 0; i < radioState->dwNumberOfPhys; ++i)
            {
                if (radioState->PhyRadioState[i].dot11SoftwareRadioState == dot11_radio_state_on &&
                    radioState->PhyRadioState[i].dot11HardwareRadioState == dot11_radio_state_on)
                {
                    radioOn = true;
                    break;
                }
            }
            WlanFreeMemory(data);
        }

        return radioOn;
    }

    VOID WINAPI OnWlanNotification(PWLAN_NOTIFICATION_DATA data, PVOID context)
    {
        auto* provider = static_cast<WifiStatusProvider*>(context);
        if (!provider || !provider->hwnd || !data)
            return;

        PostMessageW(provider->hwnd, WM_APP_STATUS_PROVIDER_EVENT, STATUS_PROVIDER_EVENT_WIFI, 0);
    }
}

bool WifiStatus_Init(WifiStatusProvider& provider, HWND hwnd)
{
    provider.hwnd = hwnd;

    DWORD negotiatedVersion = 0;
    if (WlanOpenHandle(WLAN_CLIENT_VERSION, nullptr, &negotiatedVersion, &provider.clientHandle) != ERROR_SUCCESS)
        return false;

    WlanRegisterNotification(
        provider.clientHandle,
        WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM,
        TRUE,
        OnWlanNotification,
        &provider,
        nullptr,
        nullptr);

    WifiStatus_Refresh(provider);
    return true;
}

void WifiStatus_Shutdown(WifiStatusProvider& provider)
{
    if (provider.clientHandle)
    {
        WlanRegisterNotification(
            provider.clientHandle,
            WLAN_NOTIFICATION_SOURCE_NONE,
            FALSE,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        WlanCloseHandle(provider.clientHandle, nullptr);
        provider.clientHandle = nullptr;
    }

    provider.hwnd = nullptr;
    provider.iconState = WifiIconState::Disconnected;
    provider.radioOn = false;
    provider.connectingSsid.clear();
    provider.connectingStartTick = 0;
}

void WifiStatus_Refresh(WifiStatusProvider& provider)
{
    provider.iconState = WifiIconState::Disconnected;
    provider.radioOn = false;
    provider.ssid.clear();
    if (!provider.clientHandle)
        return;

    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(provider.clientHandle, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces)
        return;

    ULONG bestSignal = 0;
    bool hasConnectedInterface = false;

    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
    {
        const WLAN_INTERFACE_INFO& iface = interfaces->InterfaceInfo[i];
        provider.radioOn = provider.radioOn || QueryInterfaceRadioOn(provider.clientHandle, iface.InterfaceGuid);
        if (iface.isState != wlan_interface_state_connected)
            continue;

        DWORD dataSize = 0;
        PVOID data = nullptr;
        const DWORD result = WlanQueryInterface(
            provider.clientHandle,
            &iface.InterfaceGuid,
            wlan_intf_opcode_current_connection,
            nullptr,
            &dataSize,
            &data,
            nullptr);

        if (result == ERROR_SUCCESS && dataSize >= sizeof(WLAN_CONNECTION_ATTRIBUTES))
        {
            const auto* attrs = static_cast<const WLAN_CONNECTION_ATTRIBUTES*>(data);
            const ULONG sig = attrs->wlanAssociationAttributes.wlanSignalQuality;
            if (sig > bestSignal)
            {
                bestSignal = sig;
                // Convert SSID bytes (not null-terminated) to wstring via UTF-8
                const DOT11_SSID& ssid = attrs->wlanAssociationAttributes.dot11Ssid;
                if (ssid.uSSIDLength > 0)
                {
                    const int wlen = MultiByteToWideChar(
                        CP_UTF8, 0,
                        reinterpret_cast<const char*>(ssid.ucSSID),
                        static_cast<int>(ssid.uSSIDLength),
                        nullptr, 0);
                    if (wlen > 0)
                    {
                        provider.ssid.resize(static_cast<size_t>(wlen));
                        MultiByteToWideChar(
                            CP_UTF8, 0,
                            reinterpret_cast<const char*>(ssid.ucSSID),
                            static_cast<int>(ssid.uSSIDLength),
                            &provider.ssid[0], wlen);
                    }
                }
            }
            hasConnectedInterface = true;
        }

        if (data)
            WlanFreeMemory(data);
    }

    if (interfaces)
        WlanFreeMemory(interfaces);

    if (hasConnectedInterface)
        provider.iconState = WifiStateFromSignal(bestSignal);
    else
        provider.ssid.clear();
}

WifiIconState WifiStatus_GetIconState(const WifiStatusProvider& provider)
{
    return provider.iconState;
}

bool WifiStatus_IsRadioOn(const WifiStatusProvider& provider)
{
    return provider.radioOn;
}

void WifiStatus_Toggle(WifiStatusProvider& provider)
{
    if (!provider.clientHandle) return;

    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(provider.clientHandle, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces)
        return;

    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
    {
        const WLAN_INTERFACE_INFO& iface = interfaces->InterfaceInfo[i];

        DWORD dataSize = 0;
        PVOID data = nullptr;
        if (WlanQueryInterface(provider.clientHandle, &iface.InterfaceGuid, wlan_intf_opcode_radio_state, nullptr, &dataSize, &data, nullptr) == ERROR_SUCCESS && data)
        {
            auto* pRadioState = static_cast<WLAN_RADIO_STATE*>(data);
            if (pRadioState->dwNumberOfPhys > 0)
            {
                bool isOn = (pRadioState->PhyRadioState[0].dot11SoftwareRadioState == dot11_radio_state_on);
                
                WLAN_PHY_RADIO_STATE newState = {};
                newState.dwPhyIndex = pRadioState->PhyRadioState[0].dwPhyIndex;
                newState.dot11SoftwareRadioState = isOn ? dot11_radio_state_off : dot11_radio_state_on;
                newState.dot11HardwareRadioState = pRadioState->PhyRadioState[0].dot11HardwareRadioState;
                
                WlanSetInterface(provider.clientHandle, &iface.InterfaceGuid, wlan_intf_opcode_radio_state, sizeof(WLAN_PHY_RADIO_STATE), &newState, nullptr);
            }
            WlanFreeMemory(data);
        }
    }
    WlanFreeMemory(interfaces);
}

void WifiStatus_Scan(WifiStatusProvider& provider)
{
    if (!provider.clientHandle) return;
    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(provider.clientHandle, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces)
        return;

    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
    {
        WlanScan(provider.clientHandle, &interfaces->InterfaceInfo[i].InterfaceGuid, nullptr, nullptr, nullptr);
    }
    WlanFreeMemory(interfaces);
}

void WifiStatus_Connect(WifiStatusProvider& provider, const std::wstring& ssid, const std::wstring& profileName)
{
    if (!provider.clientHandle || profileName.empty()) return;
    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(provider.clientHandle, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces)
        return;

    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
    {
        const WLAN_INTERFACE_INFO& iface = interfaces->InterfaceInfo[i];

        WLAN_CONNECTION_PARAMETERS params = {};
        params.wlanConnectionMode = wlan_connection_mode_profile;
        params.strProfile = profileName.c_str();
        params.pDot11Ssid = nullptr;
        params.pDesiredBssidList = nullptr;
        params.dot11BssType = dot11_BSS_type_any;
        params.dwFlags = 0;

        const DWORD result = WlanConnect(provider.clientHandle, &iface.InterfaceGuid, &params, nullptr);
        if (result == ERROR_SUCCESS)
        {
            provider.connectingSsid = ssid;
            provider.connectingStartTick = GetTickCount64();
            break;
        }
    }
    WlanFreeMemory(interfaces);
}

void WifiStatus_PopulateNetworks(WifiStatusProvider& provider, std::vector<WifiNetwork>& networks)
{
    networks.clear();
    if (!provider.clientHandle) return;

    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(provider.clientHandle, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces)
        return;

    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
    {
        const WLAN_INTERFACE_INFO& iface = interfaces->InterfaceInfo[i];
        
        constexpr ULONGLONG CONNECTING_TIMEOUT_MS = 15000;
        const bool connectingExpired = !provider.connectingSsid.empty() &&
            provider.connectingStartTick != 0 &&
            (GetTickCount64() - provider.connectingStartTick > CONNECTING_TIMEOUT_MS);
        if (connectingExpired)
        {
            provider.connectingSsid.clear();
            provider.connectingStartTick = 0;
        }

        PWLAN_AVAILABLE_NETWORK_LIST availableNetworks = nullptr;
        if (WlanGetAvailableNetworkList(provider.clientHandle, &iface.InterfaceGuid, 0, nullptr, &availableNetworks) == ERROR_SUCCESS && availableNetworks)
        {
            for (DWORD j = 0; j < availableNetworks->dwNumberOfItems; ++j)
            {
                const WLAN_AVAILABLE_NETWORK& net = availableNetworks->Network[j];
                if (net.dot11Ssid.uSSIDLength == 0) continue;

                WifiNetwork wnet;
                wnet.profileName = net.strProfileName;
                wnet.signalQuality = net.wlanSignalQuality;
                wnet.isConnected = (net.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
                wnet.isSecure = net.dot11DefaultAuthAlgorithm != DOT11_AUTH_ALGO_80211_OPEN;
                wnet.hasProfile = (net.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;

                int wlen = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(net.dot11Ssid.ucSSID), static_cast<int>(net.dot11Ssid.uSSIDLength), nullptr, 0);
                if (wlen > 0)
                {
                    wnet.ssid.resize(wlen);
                    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(net.dot11Ssid.ucSSID), static_cast<int>(net.dot11Ssid.uSSIDLength), &wnet.ssid[0], wlen);
                }

                wnet.isConnecting = (!provider.connectingSsid.empty() && wnet.ssid == provider.connectingSsid);
                if (wnet.isConnected && wnet.ssid == provider.connectingSsid) {
                    provider.connectingSsid.clear();
                    provider.connectingStartTick = 0;
                }

                auto it = std::find_if(networks.begin(), networks.end(), [&](const WifiNetwork& existing) {
                    return existing.ssid == wnet.ssid;
                });

                if (it != networks.end())
                {
                    if (wnet.signalQuality > it->signalQuality)
                        it->signalQuality = wnet.signalQuality;
                    if (wnet.isConnected)
                        it->isConnected = true;
                    if (wnet.hasProfile)
                    {
                        it->hasProfile = true;
                        it->profileName = wnet.profileName;
                    }
                    if (wnet.isConnecting)
                        it->isConnecting = true;
                }
                else
                {
                    networks.push_back(wnet);
                }
            }
            WlanFreeMemory(availableNetworks);
        }
    }
    WlanFreeMemory(interfaces);

    std::sort(networks.begin(), networks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.isConnected && !b.isConnected) return true;
        if (!a.isConnected && b.isConnected) return false;
        return a.signalQuality > b.signalQuality;
    });
}

