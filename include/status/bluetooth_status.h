#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "status/status_types.h"

struct BluetoothStatusProvider
{
    BluetoothIconState iconState = BluetoothIconState::Off;
    std::wstring connectedDeviceName; // Name of connected BT device, empty when none
    std::vector<BluetoothDevice> pairedDevices;
    std::wstring pendingDeviceAddressString;
    bool pendingDeviceTargetConnected = false;
    ULONGLONG pendingDeviceStartTick = 0;
};

bool BluetoothStatus_Init(BluetoothStatusProvider& provider);
void BluetoothStatus_Shutdown(BluetoothStatusProvider& provider);
void BluetoothStatus_Refresh(BluetoothStatusProvider& provider);
BluetoothIconState BluetoothStatus_GetIconState(const BluetoothStatusProvider& provider);
void BluetoothStatus_Toggle(BluetoothStatusProvider& provider);
bool BluetoothStatus_HasPendingDeviceOperation(const BluetoothStatusProvider& provider);
bool BluetoothStatus_BeginDeviceOperation(BluetoothStatusProvider& provider, const std::wstring& addressString, bool targetConnected);
void BluetoothStatus_ConnectDevice(BluetoothStatusProvider& provider, const std::wstring& addressString);
void BluetoothStatus_DisconnectDevice(BluetoothStatusProvider& provider, const std::wstring& addressString);
