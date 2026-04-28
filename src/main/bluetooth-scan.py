"""
bluetooth-scan.py — enumerate Bluetooth devices and radio state.

Device enumeration: bthprops.dll (same path as Windows BT stack).
  KEY FIX: hRadio must be NULL so Windows searches all available radios.
  Passing a specific radio handle corrupts the search params struct on
  64-bit because HANDLE alignment padding is consumed incorrectly.

Radio state: Windows.Devices.Radios WinRT (no admin, same as Settings).

Requirements:
  pip install winrt-Windows.Devices.Radios winrt-Windows.Foundation

Outputs JSON to stdout:
  { "enabled": bool, "connected": bool,
    "devices": [{ "id", "name", "connected", "paired" }] }

Flags:
  --state    Radio state + quick connected-device check (fast)
  --no-scan  Paired devices only — skip Bluetooth inquiry for new devices
"""

import sys
import io

if sys.stdout is None:
    sys.stdout = io.TextIOWrapper(io.FileIO(1, closefd=False), encoding='utf-8', line_buffering=True)
elif hasattr(sys.stdout, 'buffer'):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', line_buffering=True)
if sys.stderr is None:
    sys.stderr = io.TextIOWrapper(io.FileIO(2, closefd=False), encoding='utf-8', line_buffering=True)
elif hasattr(sys.stderr, 'buffer'):
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', line_buffering=True)

import asyncio
import ctypes
import ctypes.wintypes as wt
import json

# ── bthprops.dll structs ───────────────────────────────────────────────────────

_bt = ctypes.WinDLL('bthprops.cpl')

BLUETOOTH_MAX_NAME_SIZE = 248


class SYSTEMTIME(ctypes.Structure):
    _fields_ = [
        ('wYear', wt.WORD), ('wMonth', wt.WORD), ('wDayOfWeek', wt.WORD),
        ('wDay',  wt.WORD), ('wHour',  wt.WORD), ('wMinute',    wt.WORD),
        ('wSecond', wt.WORD), ('wMilliseconds', wt.WORD),
    ]


class BLUETOOTH_ADDRESS(ctypes.Structure):
    _fields_ = [('ullLong', ctypes.c_uint64)]

    def to_id(self) -> str:
        b = self.ullLong
        return ':'.join(f'{(b >> (i * 8)) & 0xFF:02X}' for i in range(5, -1, -1))


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
    # sizeof must be 40 on 64-bit Windows — ctypes adds 7 bytes padding before hRadio.
    # CRITICAL: set hRadio = None (NULL) to search all radios.
    # Passing a specific radio handle value causes incorrect struct layout on 64-bit.
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


# ── Function signatures ────────────────────────────────────────────────────────

_bt.BluetoothFindFirstRadio.restype  = wt.HANDLE
_bt.BluetoothFindFirstRadio.argtypes = [
    ctypes.POINTER(BLUETOOTH_FIND_RADIO_PARAMS),
    ctypes.POINTER(wt.HANDLE),
]
_bt.BluetoothFindRadioClose.restype  = wt.BOOL
_bt.BluetoothFindRadioClose.argtypes = [wt.HANDLE]

_bt.BluetoothFindFirstDevice.restype  = wt.HANDLE
_bt.BluetoothFindFirstDevice.argtypes = [
    ctypes.POINTER(BLUETOOTH_DEVICE_SEARCH_PARAMS),
    ctypes.POINTER(BLUETOOTH_DEVICE_INFO),
]
_bt.BluetoothFindNextDevice.restype  = wt.BOOL
_bt.BluetoothFindNextDevice.argtypes = [wt.HANDLE, ctypes.POINTER(BLUETOOTH_DEVICE_INFO)]

_bt.BluetoothFindDeviceClose.restype  = wt.BOOL
_bt.BluetoothFindDeviceClose.argtypes = [wt.HANDLE]

# ── Radio state via WinRT ──────────────────────────────────────────────────────

import winrt.windows.devices.radios as wdr


async def _winrt_radio_enabled() -> bool:
    """Check BT radio state via WinRT (reliable, no admin needed)."""
    try:
        all_radios = await wdr.Radio.get_radios_async()
        for r in all_radios:
            if r.kind == wdr.RadioKind.BLUETOOTH:
                return r.state == wdr.RadioState.ON
    except Exception:
        pass
    # Fallback: check via bthprops — if radio opens, BT is enabled
    rp = BLUETOOTH_FIND_RADIO_PARAMS()
    rp.dwSize = ctypes.sizeof(BLUETOOTH_FIND_RADIO_PARAMS)
    radio = wt.HANDLE()
    fh = _bt.BluetoothFindFirstRadio(ctypes.byref(rp), ctypes.byref(radio))
    if fh:
        _bt.BluetoothFindRadioClose(fh)
        ctypes.windll.kernel32.CloseHandle(radio)
        return True
    return False

# ── Device enumeration via bthprops.dll ───────────────────────────────────────


def _enum_devices(authenticated: bool, remembered: bool,
                  unknown: bool, connected: bool,
                  issue_inquiry: bool, timeout_mult: int = 0) -> list:
    """
    Enumerate BT devices. hRadio is always NULL — searches all radios.
    This is the critical fix: passing a specific HANDLE breaks alignment.
    """
    params = BLUETOOTH_DEVICE_SEARCH_PARAMS()
    params.dwSize               = ctypes.sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS)
    params.fReturnAuthenticated = authenticated
    params.fReturnRemembered    = remembered
    params.fReturnUnknown       = unknown
    params.fReturnConnected     = connected
    params.fIssueInquiry        = issue_inquiry
    params.cTimeoutMultiplier   = timeout_mult
    params.hRadio               = None  # NULL → search all radios

    device = BLUETOOTH_DEVICE_INFO()
    device.dwSize = ctypes.sizeof(BLUETOOTH_DEVICE_INFO)

    results = []
    find_h = _bt.BluetoothFindFirstDevice(ctypes.byref(params), ctypes.byref(device))
    if not find_h:
        return results

    try:
        while True:
            name = device.szName.strip() if device.szName else ''
            if name:
                results.append({
                    'id':        device.Address.to_id(),
                    'name':      name,
                    'connected': bool(device.fConnected),
                    'paired':    bool(device.fAuthenticated) or bool(device.fRemembered),
                })
            nxt = BLUETOOTH_DEVICE_INFO()
            nxt.dwSize = ctypes.sizeof(BLUETOOTH_DEVICE_INFO)
            if not _bt.BluetoothFindNextDevice(find_h, ctypes.byref(nxt)):
                break
            device = nxt
    finally:
        _bt.BluetoothFindDeviceClose(find_h)

    return results


# ── Main logic ─────────────────────────────────────────────────────────────────

async def check_state() -> dict:
    enabled = await _winrt_radio_enabled()
    if not enabled:
        return {'enabled': False, 'connected': False}

    # Quick connected-device check — no inquiry
    try:
        devs = _enum_devices(authenticated=True, remembered=True,
                             unknown=False, connected=True,
                             issue_inquiry=False)
        connected = any(d['connected'] for d in devs)
    except Exception:
        connected = False

    return {'enabled': True, 'connected': connected}


async def scan_devices(force: bool = True) -> dict:
    enabled = await _winrt_radio_enabled()
    if not enabled:
        return {'enabled': False, 'connected': False, 'devices': []}

    # Paired / remembered devices (fast, no inquiry)
    try:
        paired = _enum_devices(authenticated=True, remembered=True,
                               unknown=False, connected=True,
                               issue_inquiry=False)
    except Exception as e:
        print(f'[bt-scan] paired enum error: {e}', file=sys.stderr)
        paired = []

    seen_ids = {d['id'] for d in paired}
    connected_any = any(d['connected'] for d in paired)

    # New / nearby devices via Bluetooth inquiry (classic BT, blocks ~2.6 s)
    nearby: list = []
    if force:
        try:
            raw = _enum_devices(authenticated=False, remembered=False,
                                unknown=True, connected=False,
                                issue_inquiry=True, timeout_mult=2)
            nearby = [d for d in raw if d['id'] not in seen_ids]
        except Exception as e:
            print(f'[bt-scan] inquiry error: {e}', file=sys.stderr)

    paired.sort(key=lambda d: not d['connected'])
    nearby.sort(key=lambda d: not d['connected'])

    return {'enabled': True, 'connected': connected_any,
            'devices': paired + nearby}


if __name__ == '__main__':
    try:
        if '--state' in sys.argv:
            result = asyncio.run(check_state())
        else:
            force  = '--no-scan' not in sys.argv
            result = asyncio.run(scan_devices(force=force))
        print(json.dumps(result), flush=True)
    except Exception as e:
        print(json.dumps({'enabled': False, 'connected': False, 'devices': []}), flush=True)
        print(str(e), file=sys.stderr, flush=True)
