"""
bluetooth-connect.py — connect or disconnect a paired Bluetooth device.

Enables/disables the A2DP Sink and HFP services on the target device via
bthprops.dll BluetoothSetServiceState (no admin required for paired devices).

Usage:
  python bluetooth-connect.py --address AA:BB:CC:DD:EE:FF --action connect
  python bluetooth-connect.py --address AA:BB:CC:DD:EE:FF --action disconnect
"""

import sys
import io
import ctypes
import ctypes.wintypes as wt

if sys.stdout is None:
    sys.stdout = io.TextIOWrapper(io.FileIO(1, closefd=False), encoding='utf-8', line_buffering=True)
if sys.stderr is None:
    sys.stderr = io.TextIOWrapper(io.FileIO(2, closefd=False), encoding='utf-8', line_buffering=True)

_bt = ctypes.WinDLL('bthprops.cpl')
_k32 = ctypes.windll.kernel32

BLUETOOTH_MAX_NAME_SIZE = 248
BLUETOOTH_SERVICE_ENABLE  = 0x00000001
BLUETOOTH_SERVICE_DISABLE = 0x00000000

# Common audio service GUIDs (A2DP Sink, HFP HF, HSP HS)
SERVICE_GUIDS = [
    '{0000110b-0000-1000-8000-00805f9b34fb}',  # A2DP Sink
    '{0000111e-0000-1000-8000-00805f9b34fb}',  # HFP Hands-Free
    '{00001108-0000-1000-8000-00805f9b34fb}',  # HSP Headset
    '{00001124-0000-1000-8000-00805f9b34fb}',  # HID (keyboard/mouse)
]

# ── Structs ───────────────────────────────────────────────────────────────────

class SYSTEMTIME(ctypes.Structure):
    _fields_ = [
        ('wYear', wt.WORD), ('wMonth', wt.WORD), ('wDayOfWeek', wt.WORD),
        ('wDay',  wt.WORD), ('wHour',  wt.WORD), ('wMinute',    wt.WORD),
        ('wSecond', wt.WORD), ('wMilliseconds', wt.WORD),
    ]

class BLUETOOTH_ADDRESS(ctypes.Structure):
    _fields_ = [('ullLong', ctypes.c_uint64)]

class BLUETOOTH_DEVICE_INFO(ctypes.Structure):
    _fields_ = [
        ('dwSize',          wt.DWORD),
        ('Address',         BLUETOOTH_ADDRESS),
        ('ulClassofDevice', wt.ULONG),
        ('fConnected',      wt.BOOL),
        ('fRemembered',     wt.BOOL),
        ('fAuthenticated',  wt.BOOL),
        ('stLastSeen',      SYSTEMTIME),
        ('stLastUsed',      SYSTEMTIME),
        ('szName',          ctypes.c_wchar * BLUETOOTH_MAX_NAME_SIZE),
    ]

class BLUETOOTH_DEVICE_SEARCH_PARAMS(ctypes.Structure):
    _fields_ = [
        ('dwSize',               wt.DWORD),
        ('fReturnAuthenticated', wt.BOOL),
        ('fReturnRemembered',    wt.BOOL),
        ('fReturnUnknown',       wt.BOOL),
        ('fReturnConnected',     wt.BOOL),
        ('fIssueInquiry',        wt.BOOL),
        ('cTimeoutMultiplier',   ctypes.c_ubyte),
        ('hRadio',               wt.HANDLE),
    ]

class BLUETOOTH_FIND_RADIO_PARAMS(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD)]

class GUID(ctypes.Structure):
    _fields_ = [
        ('Data1', wt.ULONG),
        ('Data2', wt.USHORT),
        ('Data3', wt.USHORT),
        ('Data4', ctypes.c_ubyte * 8),
    ]

# ── Function signatures ────────────────────────────────────────────────────────

_bt.BluetoothFindFirstRadio.restype  = wt.HANDLE
_bt.BluetoothFindFirstRadio.argtypes = [ctypes.POINTER(BLUETOOTH_FIND_RADIO_PARAMS), ctypes.POINTER(wt.HANDLE)]
_bt.BluetoothFindRadioClose.restype  = wt.BOOL
_bt.BluetoothFindRadioClose.argtypes = [wt.HANDLE]
_bt.BluetoothFindFirstDevice.restype  = wt.HANDLE
_bt.BluetoothFindFirstDevice.argtypes = [ctypes.POINTER(BLUETOOTH_DEVICE_SEARCH_PARAMS), ctypes.POINTER(BLUETOOTH_DEVICE_INFO)]
_bt.BluetoothFindNextDevice.restype  = wt.BOOL
_bt.BluetoothFindNextDevice.argtypes = [wt.HANDLE, ctypes.POINTER(BLUETOOTH_DEVICE_INFO)]
_bt.BluetoothFindDeviceClose.restype  = wt.BOOL
_bt.BluetoothFindDeviceClose.argtypes = [wt.HANDLE]
_bt.BluetoothSetServiceState.restype  = wt.DWORD
_bt.BluetoothSetServiceState.argtypes = [
    wt.HANDLE,
    ctypes.POINTER(BLUETOOTH_DEVICE_INFO),
    ctypes.POINTER(GUID),
    wt.DWORD,
]

# ── Helpers ────────────────────────────────────────────────────────────────────

def addr_str_to_int(addr: str) -> int:
    parts = addr.split(':')
    result = 0
    for i, p in enumerate(reversed(parts)):
        result |= int(p, 16) << (i * 8)
    return result


def parse_guid(guid_str: str) -> GUID:
    s = guid_str.strip('{}').split('-')
    g = GUID()
    g.Data1 = int(s[0], 16)
    g.Data2 = int(s[1], 16)
    g.Data3 = int(s[2], 16)
    d4 = bytes.fromhex(s[3] + s[4])
    for i, b in enumerate(d4):
        g.Data4[i] = b
    return g


def run(address: str, action: str):
    target_int = addr_str_to_int(address)

    # Open first radio — needed for BluetoothSetServiceState (requires valid handle)
    rp = BLUETOOTH_FIND_RADIO_PARAMS()
    rp.dwSize = ctypes.sizeof(BLUETOOTH_FIND_RADIO_PARAMS)
    radio = wt.HANDLE()
    find_radio = _bt.BluetoothFindFirstRadio(ctypes.byref(rp), ctypes.byref(radio))
    if not find_radio:
        print('No Bluetooth radio found', file=sys.stderr)
        sys.exit(1)

    try:
        # CRITICAL: hRadio must be None (NULL) for BluetoothFindFirstDevice on 64-bit.
        # Passing a specific HANDLE value corrupts the struct alignment — no devices found.
        params = BLUETOOTH_DEVICE_SEARCH_PARAMS()
        params.dwSize               = ctypes.sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS)
        params.fReturnAuthenticated = True
        params.fReturnRemembered    = True
        params.fReturnUnknown       = False
        params.fReturnConnected     = True
        params.fIssueInquiry        = False
        params.cTimeoutMultiplier   = 0
        params.hRadio               = None  # NULL → search all radios

        device = BLUETOOTH_DEVICE_INFO()
        device.dwSize = ctypes.sizeof(BLUETOOTH_DEVICE_INFO)

        find_dev = _bt.BluetoothFindFirstDevice(ctypes.byref(params), ctypes.byref(device))
        if not find_dev:
            print('No paired devices found', file=sys.stderr)
            sys.exit(1)

        target_device = None
        try:
            while True:
                if device.Address.ullLong == target_int:
                    target_device = BLUETOOTH_DEVICE_INFO()
                    ctypes.memmove(ctypes.byref(target_device), ctypes.byref(device),
                                   ctypes.sizeof(BLUETOOTH_DEVICE_INFO))
                    break
                nxt = BLUETOOTH_DEVICE_INFO()
                nxt.dwSize = ctypes.sizeof(BLUETOOTH_DEVICE_INFO)
                if not _bt.BluetoothFindNextDevice(find_dev, ctypes.byref(nxt)):
                    break
                device = nxt
        finally:
            _bt.BluetoothFindDeviceClose(find_dev)

        if target_device is None:
            print(f'Device {address} not in paired list', file=sys.stderr)
            sys.exit(1)

        flag = BLUETOOTH_SERVICE_ENABLE if action == 'connect' else BLUETOOTH_SERVICE_DISABLE
        any_ok = False

        for guid_str in SERVICE_GUIDS:
            try:
                guid = parse_guid(guid_str)
                ret = _bt.BluetoothSetServiceState(
                    radio,
                    ctypes.byref(target_device),
                    ctypes.byref(guid),
                    flag,
                )
                if ret == 0:   # ERROR_SUCCESS
                    any_ok = True
            except Exception:
                pass  # Service not supported on this device — skip

        if not any_ok:
            print(f'BluetoothSetServiceState failed for all services', file=sys.stderr)
            sys.exit(1)

    finally:
        _bt.BluetoothFindRadioClose(find_radio)
        _k32.CloseHandle(radio)


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument('--address', required=True)
    p.add_argument('--action',  choices=['connect', 'disconnect'], required=True)
    args = p.parse_args()
    try:
        run(args.address, args.action)
    except Exception as e:
        print(str(e), file=sys.stderr, flush=True)
        sys.exit(1)
