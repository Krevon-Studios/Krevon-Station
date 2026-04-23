"""
audio-monitor.py — zero-latency, zero-polling.

Uses pycaw's IAudioEndpointVolumeCallback (IID B1136C83...) which is what
pycaw's RegisterControlChangeNotify actually expects. OnNotify() is called
by the OS audio thread the instant volume/mute changes. A kernel auto-reset
Event bridges the COM STA thread to the main thread — CPU usage ~0%.
"""
import json
import ctypes
import ctypes.wintypes as wt
import threading
import comtypes
from comtypes import COMObject, CLSCTX_ALL
from pycaw.pycaw import (AudioUtilities, IAudioEndpointVolume,
                          IAudioEndpointVolumeCallback,
                          AUDIO_VOLUME_NOTIFICATION_DATA)

_k32   = ctypes.windll.kernel32
_u32   = ctypes.windll.user32
_event = _k32.CreateEventW(None, False, False, None)   # auto-reset kernel event

# ── Callback — uses pycaw's exact IAudioEndpointVolumeCallback definition ────

class _Callback(COMObject):
    _com_interfaces_ = [IAudioEndpointVolumeCallback]

    def OnNotify(self, pNotify):
        _k32.SetEvent(_event)   # wake the main thread — instant, thread-safe
        return 0

# ── COM STA thread ────────────────────────────────────────────────────────────

_epv  = None
_lock = threading.Lock()

def _com_thread():
    global _epv
    comtypes.CoInitializeEx(comtypes.COINIT_APARTMENTTHREADED)
    cb = None
    try:
        speakers  = AudioUtilities.GetSpeakers()
        interface = speakers._dev.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        epv       = ctypes.cast(interface, ctypes.POINTER(IAudioEndpointVolume))
        cb        = _Callback()
        epv.RegisterControlChangeNotify(cb)
        with _lock:
            _epv = epv
        _k32.SetEvent(_event)          # emit initial state right away

        # STA message loop — COM delivers callbacks via the message queue
        msg = wt.MSG()
        while _u32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
            _u32.TranslateMessage(ctypes.byref(msg))
            _u32.DispatchMessageW(ctypes.byref(msg))

        epv.UnregisterControlChangeNotify(cb)
    except Exception as e:
        import sys
        print(f'[audio COM thread error] {e}', file=sys.stderr, flush=True)
    finally:
        comtypes.CoUninitialize()

def emit(vol, mute):
    print(json.dumps({'volume': vol, 'muted': mute}), flush=True)

def main():
    threading.Thread(target=_com_thread, daemon=True).start()
    INFINITE = 0xFFFFFFFF
    while True:
        _k32.WaitForSingleObject(_event, INFINITE)   # zero CPU until OS fires
        with _lock:
            epv = _epv
        if epv:
            emit(round(epv.GetMasterVolumeLevelScalar() * 100),
                 bool(epv.GetMute()))

if __name__ == '__main__':
    main()
