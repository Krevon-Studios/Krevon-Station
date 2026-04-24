"""
wifi-toggle.py — toggle WiFi radio state without admin rights via wlanapi.dll
"""

import ctypes
import ctypes.wintypes as wt
import sys

_wlan = ctypes.WinDLL('wlanapi.dll')

# Basic structs
class GUID(ctypes.Structure):
    _fields_ = [
        ('Data1', wt.ULONG),
        ('Data2', wt.USHORT),
        ('Data3', wt.USHORT),
        ('Data4', ctypes.c_ubyte * 8),
    ]

class WLAN_INTERFACE_INFO(ctypes.Structure):
    _fields_ = [
        ('InterfaceGuid', GUID),
        ('strInterfaceDescription', ctypes.c_wchar * 256),
        ('isState', ctypes.c_uint),
    ]

class WLAN_INTERFACE_INFO_LIST(ctypes.Structure):
    _fields_ = [
        ('dwNumberOfItems', wt.DWORD),
        ('dwIndex',         wt.DWORD),
        ('InterfaceInfo',   WLAN_INTERFACE_INFO * 1),
    ]

class WLAN_PHY_RADIO_STATE(ctypes.Structure):
    _fields_ = [
        ('dwPhyIndex', wt.DWORD),
        ('dot11SoftwareRadioState', wt.DWORD),
        ('dot11HardwareRadioState', wt.DWORD),
    ]

_wlan.WlanOpenHandle.restype = wt.DWORD
_wlan.WlanCloseHandle.restype = wt.DWORD
_wlan.WlanEnumInterfaces.restype = wt.DWORD
_wlan.WlanFreeMemory.restype = None
_wlan.WlanSetInterface.restype = wt.DWORD

def set_wifi_state(enable: bool):
    handle = wt.HANDLE()
    version = wt.DWORD()
    ret = _wlan.WlanOpenHandle(2, None, ctypes.byref(version), ctypes.byref(handle))
    if ret != 0:
        sys.exit(ret)
        
    try:
        iface_list_ptr = ctypes.POINTER(WLAN_INTERFACE_INFO_LIST)()
        ret = _wlan.WlanEnumInterfaces(handle, None, ctypes.byref(iface_list_ptr))
        if ret != 0 or not iface_list_ptr:
            sys.exit(ret)
            
        n_ifaces = iface_list_ptr.contents.dwNumberOfItems
        if n_ifaces == 0:
            _wlan.WlanFreeMemory(iface_list_ptr)
            sys.exit(0)
            
        ifaces = (WLAN_INTERFACE_INFO * n_ifaces).from_address(ctypes.addressof(iface_list_ptr.contents.InterfaceInfo))
        
        # wlan_intf_opcode_radio_state = 4
        # dot11_radio_state_on = 1, dot11_radio_state_off = 2
        
        for i in range(n_ifaces):
            guid_ptr = ctypes.pointer(ifaces[i].InterfaceGuid)
            state = WLAN_PHY_RADIO_STATE()
            # Set for all PHYs (or typically PHY 0 is enough)
            state.dwPhyIndex = 0
            state.dot11SoftwareRadioState = 1 if enable else 2
            
            _wlan.WlanSetInterface(
                handle,
                guid_ptr,
                4,  # wlan_intf_opcode_radio_state
                ctypes.sizeof(state),
                ctypes.byref(state),
                None
            )
            
        _wlan.WlanFreeMemory(iface_list_ptr)
    finally:
        _wlan.WlanCloseHandle(handle, None)

if __name__ == '__main__':
    if '--enable' in sys.argv:
        set_wifi_state(True)
    elif '--disable' in sys.argv:
        set_wifi_state(False)
