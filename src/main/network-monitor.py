"""
network-monitor.py — hybrid event-driven + polling network monitor.

Uses NotifyAddrChange() on a background thread for instant disconnect
detection, combined with a 3-second polling loop to smoothly track
signal strength changes over time.
"""
import json
import ctypes
import ctypes.wintypes as wt
import subprocess
import socket
import re
import psutil
import time
import threading

# ── State queries ─────────────────────────────────────────────────────────────

def _wifi_info():
    """Returns (ssid, signal%) if WiFi is connected, else (None, None)."""
    try:
        out = subprocess.run(
            ['netsh', 'wlan', 'show', 'interfaces'],
            capture_output=True, text=True, timeout=3,
            creationflags=0x08000000  # CREATE_NO_WINDOW
        ).stdout
        if 'connected' not in (re.search(r'State\s*:\s*(.+)', out) or ('',))[0].lower():
            return None, None
        ssid   = (re.search(r'^\s+SSID\s+:\s+(.+)$',   out, re.M) or [None, None])[1]
        signal = (re.search(r'^\s+Signal\s+:\s+(\d+)%', out, re.M) or [None, None])[1]
        return (ssid.strip() if ssid else None,
                int(signal)  if signal else None)
    except Exception:
        return None, None

# VPN adapter keywords — covers most common Windows VPN clients
_VPN_KEYWORDS = (
    'vpn', 'tap', 'tun', 'wireguard', 'nordvpn', 'expressvpn',
    'protonvpn', 'mullvad', 'openvpn', 'cisco anyconnect', 'globalprotect',
    'pulse secure', 'fortinet', 'ppp', 'l2tp', 'sstp', 'ikev2',
)

def _has_vpn():
    """True if any VPN-related adapter is currently up."""
    stats = psutil.net_if_stats()
    for name, stat in stats.items():
        if stat.isup and any(kw in name.lower() for kw in _VPN_KEYWORDS):
            return True
    return False

def _check_internet():
    """Fast TCP probe — 500 ms timeout so UI updates feel instant."""
    try:
        s = socket.create_connection(('1.1.1.1', 443), timeout=0.5)
        s.close()
        return True
    except Exception:
        return False

def get_state():
    ssid, signal = _wifi_info()
    vpn = _has_vpn()
    if ssid is not None:
        return {'type': 'wifi',     'signal': signal, 'ssid': ssid,
                'hasInternet': _check_internet(), 'vpnActive': vpn}
    return     {'type': 'none',    'signal': None,   'ssid': None,
                'hasInternet': False, 'vpnActive': vpn}

def emit(state):
    print(json.dumps(state), flush=True)

def event_listener(wake_event):
    _iphlpapi = ctypes.WinDLL('iphlpapi.dll')
    _iphlpapi.NotifyAddrChange.restype  = ctypes.c_ulong
    _iphlpapi.NotifyAddrChange.argtypes = [
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.c_void_p,
    ]
    handle = ctypes.c_void_p(0)
    while True:
        _iphlpapi.NotifyAddrChange(ctypes.byref(handle), None)
        # Windows needs a tiny moment to update its internal state
        time.sleep(0.5)
        wake_event.set()

def main():
    wake_event = threading.Event()
    threading.Thread(target=event_listener, args=(wake_event,), daemon=True).start()

    last = get_state()
    emit(last)

    while True:
        wake_event.wait(3.0)
        wake_event.clear()
        current = get_state()
        if current != last:
            last = current
            emit(last)

if __name__ == '__main__':
    main()
