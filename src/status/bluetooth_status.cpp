#include "status/bluetooth_status.h"
#include <bluetoothapis.h>
#include <string>
#include <thread>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Radios.h>

namespace
{
    constexpr ULONGLONG BT_DEVICE_OPERATION_TIMEOUT_MS = 15000;

    std::wstring FormatBthAddr(BTH_ADDR addr)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%012llX", addr);
        return buf;
    }

    BTH_ADDR ParseBthAddr(const std::wstring& str)
    {
        return std::stoull(str, nullptr, 16);
    }

    std::vector<BluetoothDevice> GetPairedBluetoothDevices()
    {
        std::vector<BluetoothDevice> devices;
        BLUETOOTH_FIND_RADIO_PARAMS radioParams = {};
        radioParams.dwSize = sizeof(radioParams);

        HANDLE radioHandle = nullptr;
        HBLUETOOTH_RADIO_FIND radioFind = BluetoothFindFirstRadio(&radioParams, &radioHandle);
        if (!radioFind)
            return devices;

        HANDLE currentRadio = radioHandle;

        do
        {
            BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {};
            searchParams.dwSize = sizeof(searchParams);
            searchParams.fReturnAuthenticated = TRUE;
            searchParams.fReturnRemembered = TRUE;
            searchParams.fReturnConnected = TRUE;
            searchParams.fReturnUnknown = FALSE;
            searchParams.fIssueInquiry = FALSE;
            searchParams.cTimeoutMultiplier = 0;
            searchParams.hRadio = currentRadio;

            BLUETOOTH_DEVICE_INFO deviceInfo = {};
            deviceInfo.dwSize = sizeof(deviceInfo);

            HBLUETOOTH_DEVICE_FIND deviceFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
            if (deviceFind)
            {
                do
                {
                    bool exists = false;
                    std::wstring addrStr = FormatBthAddr(deviceInfo.Address.ullLong);
                    for (const auto& d : devices) {
                        if (d.addressString == addrStr) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        BluetoothDevice dev;
                        dev.name = deviceInfo.szName;
                        dev.addressString = addrStr;
                        dev.isConnected = deviceInfo.fConnected;
                        dev.isConnecting = false;
                        dev.isDisconnecting = false;
                        devices.push_back(dev);
                    }
                    
                    deviceInfo.dwSize = sizeof(deviceInfo);
                } while (BluetoothFindNextDevice(deviceFind, &deviceInfo));

                BluetoothFindDeviceClose(deviceFind);
            }

            CloseHandle(currentRadio);
            currentRadio = nullptr;
        } while (BluetoothFindNextRadio(radioFind, &currentRadio));

        if (currentRadio)
            CloseHandle(currentRadio);
        BluetoothFindRadioClose(radioFind);
        return devices;
    }

    bool IsBluetoothRadioAvailable()
    {
        BLUETOOTH_FIND_RADIO_PARAMS radioParams = {};
        radioParams.dwSize = sizeof(radioParams);

        HANDLE radioHandle = nullptr;
        HBLUETOOTH_RADIO_FIND radioFind = BluetoothFindFirstRadio(&radioParams, &radioHandle);
        if (!radioFind)
            return false;

        CloseHandle(radioHandle);
        BluetoothFindRadioClose(radioFind);
        return true;
    }

    bool IsPendingExpired(const BluetoothStatusProvider& provider)
    {
        return !provider.pendingDeviceAddressString.empty() &&
            provider.pendingDeviceStartTick != 0 &&
            GetTickCount64() - provider.pendingDeviceStartTick > BT_DEVICE_OPERATION_TIMEOUT_MS;
    }

    void ClearPendingDeviceOperation(BluetoothStatusProvider& provider)
    {
        provider.pendingDeviceAddressString.clear();
        provider.pendingDeviceTargetConnected = false;
        provider.pendingDeviceStartTick = 0;
    }
}

bool BluetoothStatus_Init(BluetoothStatusProvider& provider)
{
    BluetoothStatus_Refresh(provider);
    return true;
}

void BluetoothStatus_Shutdown(BluetoothStatusProvider& provider)
{
    provider.iconState = BluetoothIconState::Off;
    provider.connectedDeviceName.clear();
    provider.pairedDevices.clear();
    ClearPendingDeviceOperation(provider);
}

void BluetoothStatus_Refresh(BluetoothStatusProvider& provider)
{
    provider.pairedDevices = GetPairedBluetoothDevices();
    provider.connectedDeviceName.clear();
    provider.iconState = BluetoothIconState::Off;

    if (IsPendingExpired(provider))
        ClearPendingDeviceOperation(provider);

    if (!provider.pendingDeviceAddressString.empty())
    {
        bool foundPendingDevice = false;
        for (auto& dev : provider.pairedDevices)
        {
            if (dev.addressString != provider.pendingDeviceAddressString)
                continue;

            foundPendingDevice = true;
            if (dev.isConnected == provider.pendingDeviceTargetConnected)
            {
                ClearPendingDeviceOperation(provider);
            }
            else
            {
                dev.isConnecting = provider.pendingDeviceTargetConnected;
                dev.isDisconnecting = !provider.pendingDeviceTargetConnected;
            }
            break;
        }

        if (!foundPendingDevice)
            ClearPendingDeviceOperation(provider);
    }

    bool radioOn = IsBluetoothRadioAvailable();
    if (radioOn)
    {
        provider.iconState = BluetoothIconState::On;
        for (const auto& dev : provider.pairedDevices)
        {
            if (dev.isConnected)
            {
                provider.iconState = BluetoothIconState::Connected;
                provider.connectedDeviceName = dev.name;
                break;
            }
        }
    }
}

BluetoothIconState BluetoothStatus_GetIconState(const BluetoothStatusProvider& provider)
{
    return provider.iconState;
}

void BluetoothStatus_Toggle(BluetoothStatusProvider& /*provider*/)
{
    std::thread([]() {
        winrt::init_apartment();
        try
        {
            using namespace winrt::Windows::Devices::Radios;
            auto radios = Radio::GetRadiosAsync().get();
            for (auto const& radio : radios)
            {
                if (radio.Kind() == RadioKind::Bluetooth)
                {
                    auto newState = (radio.State() == RadioState::On) ? RadioState::Off : RadioState::On;
                    radio.SetStateAsync(newState).get();
                    break;
                }
            }
        }
        catch (...)
        {
            // Ignore errors
        }
    }).detach();
}

bool BluetoothStatus_HasPendingDeviceOperation(const BluetoothStatusProvider& provider)
{
    return !provider.pendingDeviceAddressString.empty() && !IsPendingExpired(provider);
}

bool BluetoothStatus_BeginDeviceOperation(BluetoothStatusProvider& provider, const std::wstring& addressString, bool targetConnected)
{
    if (IsPendingExpired(provider))
        ClearPendingDeviceOperation(provider);

    if (!provider.pendingDeviceAddressString.empty())
        return false;

    provider.pendingDeviceAddressString = addressString;
    provider.pendingDeviceTargetConnected = targetConnected;
    provider.pendingDeviceStartTick = GetTickCount64();

    for (auto& dev : provider.pairedDevices)
    {
        dev.isConnecting = false;
        dev.isDisconnecting = false;
        if (dev.addressString == addressString)
        {
            dev.isConnecting = targetConnected;
            dev.isDisconnecting = !targetConnected;
        }
    }

    return true;
}

static void SetBluetoothDeviceState(const std::wstring& addressString, bool connect)
{
    BTH_ADDR addr = ParseBthAddr(addressString);
    HANDLE radioHandle = nullptr;
    BLUETOOTH_FIND_RADIO_PARAMS radioParams = { sizeof(radioParams) };
    HBLUETOOTH_RADIO_FIND radioFind = BluetoothFindFirstRadio(&radioParams, &radioHandle);
    if (!radioFind) return;

    BLUETOOTH_DEVICE_INFO deviceInfo = {};
    deviceInfo.dwSize = sizeof(deviceInfo);
    deviceInfo.Address.ullLong = addr;

    if (BluetoothGetDeviceInfo(radioHandle, &deviceInfo) == ERROR_SUCCESS)
    {
        // Well-known Bluetooth service GUIDs that cover the most common device types:
        // A2DP Sink, HFP HF, HFP AG, HID, AVRCP Controller, AVRCP Target, PAN, OPP
        static const GUID kServiceGuids[] = {
            // A2DP Sink  {0000110b-0000-1000-8000-00805f9b34fb}
            { 0x0000110b, 0x0000, 0x1000, { 0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb } },
            // HFP HF     {0000111e-0000-1000-8000-00805f9b34fb}
            { 0x0000111e, 0x0000, 0x1000, { 0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb } },
            // HFP AG     {0000111f-0000-1000-8000-00805f9b34fb}
            { 0x0000111f, 0x0000, 0x1000, { 0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb } },
            // HID        {00001124-0000-1000-8000-00805f9b34fb}
            { 0x00001124, 0x0000, 0x1000, { 0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb } },
            // AVRCP Ctrl {0000110e-0000-1000-8000-00805f9b34fb}
            { 0x0000110e, 0x0000, 0x1000, { 0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb } },
            // AVRCP Tgt  {0000110c-0000-1000-8000-00805f9b34fb}
            { 0x0000110c, 0x0000, 0x1000, { 0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb } },
        };

        // First try the installed-services enumeration (works for classic BT audio devices)
        DWORD numServices = 0;
        bool usedEnumerated = false;
        if (BluetoothEnumerateInstalledServices(radioHandle, &deviceInfo, &numServices, nullptr) == ERROR_SUCCESS && numServices > 0)
        {
            std::vector<GUID> services(numServices);
            if (BluetoothEnumerateInstalledServices(radioHandle, &deviceInfo, &numServices, services.data()) == ERROR_SUCCESS)
            {
                usedEnumerated = true;
                for (DWORD i = 0; i < numServices; ++i)
                {
                    if (connect)
                    {
                        BluetoothSetServiceState(radioHandle, &deviceInfo, &services[i], BLUETOOTH_SERVICE_DISABLE);
                        BluetoothSetServiceState(radioHandle, &deviceInfo, &services[i], BLUETOOTH_SERVICE_ENABLE);
                    }
                    else
                    {
                        BluetoothSetServiceState(radioHandle, &deviceInfo, &services[i], BLUETOOTH_SERVICE_DISABLE);
                    }
                }
            }
        }

        // Fallback: blast all common GUIDs (handles HID, BLE-paired classic devices, etc.)
        if (!usedEnumerated)
        {
            for (const GUID& guid : kServiceGuids)
            {
                GUID g = guid;
                if (connect)
                {
                    BluetoothSetServiceState(radioHandle, &deviceInfo, &g, BLUETOOTH_SERVICE_DISABLE);
                    BluetoothSetServiceState(radioHandle, &deviceInfo, &g, BLUETOOTH_SERVICE_ENABLE);
                }
                else
                {
                    BluetoothSetServiceState(radioHandle, &deviceInfo, &g, BLUETOOTH_SERVICE_DISABLE);
                }
            }
        }
    }
    CloseHandle(radioHandle);
    BluetoothFindRadioClose(radioFind);
}

void BluetoothStatus_ConnectDevice(BluetoothStatusProvider& /*provider*/, const std::wstring& addressString)
{
    SetBluetoothDeviceState(addressString, true);
}

void BluetoothStatus_DisconnectDevice(BluetoothStatusProvider& /*provider*/, const std::wstring& addressString)
{
    SetBluetoothDeviceState(addressString, false);
}
