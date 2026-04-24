"""
wifi-scan.py — one-shot WiFi network scanner via Windows WLAN API.

Uses WlanOpenHandle → WlanEnumInterfaces → WlanScan → (wait) →
WlanGetAvailableNetworkList → WlanFreeMemory → WlanCloseHandle.

This is the exact same API path that Windows Settings uses, so it
always returns the full list of nearby networks (not just cached ones).

Outputs a single JSON array to stdout then exits.
"""

import ctypes
import ctypes.wintypes as wt
import json
import sys
import time

# ── wlanapi.dll bindings ───────────────────────────────────────────────────────

_wlan = ctypes.WinDLL('wlanapi.dll')

WLAN_MAX_NAME_LENGTH   = 256
DOT11_SSID_MAX_LENGTH  = 32

# GUID struct
class GUID(ctypes.Structure):
    _fields_ = [
        ('Data1', wt.ULONG),
        ('Data2', wt.USHORT),
        ('Data3', wt.USHORT),
        ('Data4', ctypes.c_ubyte * 8),
    ]

# DOT11_SSID
class DOT11_SSID(ctypes.Structure):
    _fields_ = [
        ('uSSIDLength', wt.ULONG),
        ('ucSSID',      ctypes.c_ubyte * DOT11_SSID_MAX_LENGTH),
    ]

# Dot11PhyType enum values (subset)
DOT11_PHY_TYPE_UNKNOWN = 0

# WLAN_AVAILABLE_NETWORK flags
WLAN_AVAILABLE_NETWORK_CONNECTED            = 0x00000001
WLAN_AVAILABLE_NETWORK_HAS_PROFILE         = 0x00000002

# Auth algorithms (subset — "open" means no security)
DOT11_AUTH_ALGO_80211_OPEN = 1

class WLAN_AVAILABLE_NETWORK(ctypes.Structure):
    _fields_ = [
        ('strProfileName',              ctypes.c_wchar * WLAN_MAX_NAME_LENGTH),
        ('dot11Ssid',                   DOT11_SSID),
        ('dot11BssType',                ctypes.c_uint),
        ('uNumberOfBssids',             wt.ULONG),
        ('bNetworkConnectable',         wt.BOOL),
        ('wlanNotConnectableReason',    wt.DWORD),
        ('uNumberOfPhyTypes',           wt.ULONG),
        ('dot11PhyTypes',               ctypes.c_uint * 8),
        ('bMorePhyTypes',               wt.BOOL),
        ('wlanSignalQuality',           wt.ULONG),    # 0-100
        ('bSecurityEnabled',            wt.BOOL),
        ('dot11DefaultAuthAlgorithm',   ctypes.c_uint),
        ('dot11DefaultCipherAlgorithm', ctypes.c_uint),
        ('dwFlags',                     wt.DWORD),
        ('dwReserved',                  wt.DWORD),
    ]

class WLAN_AVAILABLE_NETWORK_LIST(ctypes.Structure):
    _fields_ = [
        ('dwNumberOfItems', wt.DWORD),
        ('dwIndex',         wt.DWORD),
        ('Network',         WLAN_AVAILABLE_NETWORK * 1),  # variable length
    ]

# WLAN_INTERFACE_INFO
class WLAN_INTERFACE_INFO(ctypes.Structure):
    _fields_ = [
        ('InterfaceGuid',        GUID),
        ('strInterfaceDescription', ctypes.c_wchar * WLAN_MAX_NAME_LENGTH),
        ('isState',              ctypes.c_uint),
    ]

class WLAN_INTERFACE_INFO_LIST(ctypes.Structure):
    _fields_ = [
        ('dwNumberOfItems', wt.DWORD),
        ('dwIndex',         wt.DWORD),
        ('InterfaceInfo',   WLAN_INTERFACE_INFO * 1),
    ]

# ── Function signatures ────────────────────────────────────────────────────────

_wlan.WlanOpenHandle.restype  = wt.DWORD
_wlan.WlanOpenHandle.argtypes = [
    wt.DWORD,                          # dwClientVersion
    ctypes.c_void_p,                   # pReserved
    ctypes.POINTER(wt.DWORD),          # pdwNegotiatedVersion
    ctypes.POINTER(wt.HANDLE),         # phClientHandle
]

_wlan.WlanCloseHandle.restype  = wt.DWORD
_wlan.WlanCloseHandle.argtypes = [wt.HANDLE, ctypes.c_void_p]

_wlan.WlanEnumInterfaces.restype  = wt.DWORD
_wlan.WlanEnumInterfaces.argtypes = [
    wt.HANDLE, ctypes.c_void_p,
    ctypes.POINTER(ctypes.POINTER(WLAN_INTERFACE_INFO_LIST)),
]

_wlan.WlanScan.restype  = wt.DWORD
_wlan.WlanScan.argtypes = [
    wt.HANDLE, ctypes.POINTER(GUID),
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
]

_wlan.WlanGetAvailableNetworkList.restype  = wt.DWORD
_wlan.WlanGetAvailableNetworkList.argtypes = [
    wt.HANDLE,
    ctypes.POINTER(GUID),
    wt.DWORD,                          # dwFlags (0 = include all)
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.POINTER(WLAN_AVAILABLE_NETWORK_LIST)),
]

_wlan.WlanFreeMemory.restype  = None
_wlan.WlanFreeMemory.argtypes = [ctypes.c_void_p]

# ── Main scan logic ────────────────────────────────────────────────────────────

ERROR_SUCCESS = 0

def ssid_bytes_to_str(ssid: DOT11_SSID) -> str:
    length = ssid.uSSIDLength
    if length == 0:
        return ''
    raw = bytes(ssid.ucSSID[:length])
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('latin-1', errors='replace')


def scan() -> list:
    handle   = wt.HANDLE()
    version  = wt.DWORD()

    # Open WLAN client handle (version 2 = Vista+)
    ret = _wlan.WlanOpenHandle(2, None, ctypes.byref(version), ctypes.byref(handle))
    if ret != ERROR_SUCCESS:
        return []

    try:
        # Enumerate interfaces
        iface_list_ptr = ctypes.POINTER(WLAN_INTERFACE_INFO_LIST)()
        ret = _wlan.WlanEnumInterfaces(handle, None, ctypes.byref(iface_list_ptr))
        if ret != ERROR_SUCCESS or not iface_list_ptr:
            return []

        iface_list = iface_list_ptr.contents
        n_ifaces   = iface_list.dwNumberOfItems
        if n_ifaces == 0:
            _wlan.WlanFreeMemory(iface_list_ptr)
            return []

        # Use a raw cast to access all items (the struct only declares 1)
        ifaces = (WLAN_INTERFACE_INFO * n_ifaces).from_address(
            ctypes.addressof(iface_list.InterfaceInfo)
        )

        results = []

        for i in range(n_ifaces):
            iface    = ifaces[i]
            guid_ptr = ctypes.pointer(iface.InterfaceGuid)

            # Trigger an active scan on this interface
            _wlan.WlanScan(handle, guid_ptr, None, None, None)

        # Windows needs a moment to collect beacon responses
        time.sleep(2.5)

        seen = set()

        for i in range(n_ifaces):
            iface    = ifaces[i]
            guid_ptr = ctypes.pointer(iface.InterfaceGuid)

            net_list_ptr = ctypes.POINTER(WLAN_AVAILABLE_NETWORK_LIST)()
            # dwFlags = 0x00000002 → include networks without a profile too
            ret = _wlan.WlanGetAvailableNetworkList(
                handle, guid_ptr, 0x00000002, None, ctypes.byref(net_list_ptr)
            )
            if ret != ERROR_SUCCESS or not net_list_ptr:
                continue

            net_list = net_list_ptr.contents
            n_nets   = net_list.dwNumberOfItems

            networks = (WLAN_AVAILABLE_NETWORK * n_nets).from_address(
                ctypes.addressof(net_list.Network)
            )

            for j in range(n_nets):
                net  = networks[j]
                ssid = ssid_bytes_to_str(net.dot11Ssid)
                if not ssid or ssid in seen:
                    continue
                seen.add(ssid)

                connected = bool(net.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED)
                secured   = bool(net.bSecurityEnabled) and \
                            net.dot11DefaultAuthAlgorithm != DOT11_AUTH_ALGO_80211_OPEN
                signal    = int(net.wlanSignalQuality)  # 0–100

                results.append({
                    'ssid':      ssid,
                    'signal':    signal,
                    'secured':   secured,
                    'connected': connected,
                })

            _wlan.WlanFreeMemory(net_list_ptr)

        _wlan.WlanFreeMemory(iface_list_ptr)

        # Sort: connected first, then by signal strength descending
        results.sort(key=lambda n: (not n['connected'], -n['signal']))
        return results

    finally:
        _wlan.WlanCloseHandle(handle, None)


if __name__ == '__main__':
    try:
        print(json.dumps(scan()), flush=True)
    except Exception as e:
        print(json.dumps([]), flush=True)
        print(str(e), file=sys.stderr, flush=True)
