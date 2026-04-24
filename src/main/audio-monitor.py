"""
audio-monitor.py — zero-latency, zero-polling.

Master volume: IAudioEndpointVolumeCallback COM callback + WaitForSingleObject.
App sessions:  on-demand via {"cmd": "sessions"} stdin command.
               Responds with {"type": "sessions", "sessions": [...]}

stdin commands (JSON lines):
  {"volume": 0-100}                        → set master volume
  {"muted": true/false}                    → set master mute
  {"cmd": "sessions"}                      → emit all app sessions
  {"session": {"pid": N, "volume": V}}     → set app volume
  {"session": {"pid": N, "muted": true}}   → set app mute
"""
import json
import ctypes
import ctypes.wintypes as wt
import threading
import sys
import comtypes
from comtypes import COMObject, CLSCTX_ALL
from pycaw.pycaw import (AudioUtilities, IAudioEndpointVolume,
                          IAudioEndpointVolumeCallback,
                          AUDIO_VOLUME_NOTIFICATION_DATA,
                          ISimpleAudioVolume)

_k32   = ctypes.windll.kernel32
_u32   = ctypes.windll.user32
_event = _k32.CreateEventW(None, False, False, None)

_SKIP_PROCS = frozenset({
    'audiodg.exe', 'system', 'idle', 'svchost.exe', 'dwm.exe',
    'winlogon.exe', 'csrss.exe', 'smss.exe', 'wininit.exe',
    'services.exe', 'lsass.exe', 'rundll32.exe',
})

# ── Master volume callback ─────────────────────────────────────────────────────

class _Callback(COMObject):
    _com_interfaces_ = [IAudioEndpointVolumeCallback]

    def OnNotify(self, pNotify):
        _k32.SetEvent(_event)
        return 0

# ── COM STA thread (master volume) ────────────────────────────────────────────

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
        _k32.SetEvent(_event)

        msg = wt.MSG()
        while _u32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
            _u32.TranslateMessage(ctypes.byref(msg))
            _u32.DispatchMessageW(ctypes.byref(msg))

        epv.UnregisterControlChangeNotify(cb)
    except Exception as e:
        print(f'[audio COM thread error] {e}', file=sys.stderr, flush=True)
    finally:
        comtypes.CoUninitialize()

# ── Session helpers (each runs in its own STA thread, no message loop needed) ─

def _sessions_thread():
    comtypes.CoInitializeEx(comtypes.COINIT_APARTMENTTHREADED)
    try:
        raw    = AudioUtilities.GetAllSessions()
        seen   = set()
        result = []
        for s in raw:
            if s.Process is None:
                continue
            pid  = s.Process.pid
            if pid in seen:
                continue
            name = s.Process.name()
            if name.lower() in _SKIP_PROCS:
                continue
            try:
                vol   = s.SimpleAudioVolume
                dname = name[:-4] if name.lower().endswith('.exe') else name
                result.append({
                    'pid':    pid,
                    'name':   dname,
                    'volume': round(vol.GetMasterVolume() * 100),
                    'muted':  bool(vol.GetMute()),
                })
                seen.add(pid)
            except Exception:
                pass
        print(json.dumps({'type': 'sessions', 'sessions': result}), flush=True)
    except Exception as exc:
        print(json.dumps({'type': 'sessions', 'sessions': [], 'error': str(exc)}), flush=True)
    finally:
        comtypes.CoUninitialize()


def _set_session_thread(pid: int, volume, muted):
    comtypes.CoInitializeEx(comtypes.COINIT_APARTMENTTHREADED)
    try:
        for s in AudioUtilities.GetAllSessions():
            if s.Process and s.Process.pid == pid:
                vol = s.SimpleAudioVolume
                if volume is not None:
                    vol.SetMasterVolume(max(0.0, min(1.0, float(volume) / 100.0)), None)
                if muted is not None:
                    vol.SetMute(1 if muted else 0, None)
                break
    except Exception:
        pass
    finally:
        comtypes.CoUninitialize()

# ── Stdout helpers ────────────────────────────────────────────────────────────

def emit_master(vol, mute):
    print(json.dumps({'volume': vol, 'muted': mute}), flush=True)

# ── Stdin command handler ─────────────────────────────────────────────────────

def _stdin_thread():
    for line in sys.stdin:
        line = line.strip()
        if not line or not line.startswith('{'):
            continue
        try:
            cmd = json.loads(line)
        except Exception:
            continue

        # Master volume / mute
        if 'volume' in cmd or 'muted' in cmd:
            with _lock:
                epv = _epv
            if epv is None:
                continue
            try:
                if 'volume' in cmd:
                    v = max(0.0, min(1.0, float(cmd['volume']) / 100.0))
                    epv.SetMasterVolumeLevelScalar(v, None)
                if 'muted' in cmd:
                    epv.SetMute(1 if cmd['muted'] else 0, None)
                _k32.SetEvent(_event)
            except Exception as e:
                print(f'[master vol cmd error] {e}', file=sys.stderr, flush=True)

        # Session list request
        elif cmd.get('cmd') == 'sessions':
            threading.Thread(target=_sessions_thread, daemon=True).start()

        # Per-app session volume
        elif 'session' in cmd:
            s = cmd['session']
            pid = int(s.get('pid', 0))
            if pid > 0:
                threading.Thread(
                    target=_set_session_thread,
                    args=(pid, s.get('volume'), s.get('muted')),
                    daemon=True,
                ).start()


# ── Main loop ─────────────────────────────────────────────────────────────────

def main():
    threading.Thread(target=_com_thread,   daemon=True).start()
    threading.Thread(target=_stdin_thread, daemon=True).start()

    INFINITE = 0xFFFFFFFF
    while True:
        _k32.WaitForSingleObject(_event, INFINITE)
        with _lock:
            epv = _epv
        if epv:
            emit_master(
                round(epv.GetMasterVolumeLevelScalar() * 100),
                bool(epv.GetMute()),
            )


if __name__ == '__main__':
    main()
