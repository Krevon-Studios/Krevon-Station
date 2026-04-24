"""
audio-monitor.py — zero-latency, zero-polling.

Master volume: IAudioEndpointVolumeCallback COM callback + WaitForSingleObject.
App sessions:  on-demand via {"cmd": "sessions"} stdin command.
               Responds with {"type": "sessions", "sessions": [...]}
Output devices: on-demand via {"cmd": "devices"} stdin command.
               Responds with {"type": "devices", "devices": [...], "activeId": "..."}
Set default:   {"set_device": "<endpoint-id>"}

stdin commands (JSON lines):
  {"volume": 0-100}                        → set master volume
  {"muted": true/false}                    → set master mute
  {"cmd": "sessions"}                      → emit all app sessions
  {"cmd": "devices"}                       → emit render output devices
  {"session": {"pid": N, "volume": V}}     → set app volume
  {"session": {"pid": N, "muted": true}}   → set app mute
  {"set_device": "<endpoint-id>"}          → set default render device
"""
import json
import ctypes
import ctypes.wintypes as wt
import threading
import sys
import comtypes
import comtypes.client
from comtypes import COMObject, CLSCTX_ALL, GUID, HRESULT, IUnknown
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

# ── IPolicyConfig (undocumented, works Win Vista → 11) ───────────────────────
# Used to set the Windows default audio render endpoint

class _IPolicyConfig(IUnknown):
    _iid_ = GUID('{F8679F50-850A-41CF-9C72-430F290290C8}')
    _methods_ = [
        comtypes.STDMETHOD(HRESULT, 'GetMixFormat'),
        comtypes.STDMETHOD(HRESULT, 'GetDeviceFormat'),
        comtypes.STDMETHOD(HRESULT, 'ResetDeviceFormat'),
        comtypes.STDMETHOD(HRESULT, 'SetDeviceFormat'),
        comtypes.STDMETHOD(HRESULT, 'GetProcessingPeriod'),
        comtypes.STDMETHOD(HRESULT, 'SetProcessingPeriod'),
        comtypes.STDMETHOD(HRESULT, 'GetShareMode'),
        comtypes.STDMETHOD(HRESULT, 'SetShareMode'),
        comtypes.STDMETHOD(HRESULT, 'GetPropertyValue'),
        comtypes.STDMETHOD(HRESULT, 'SetPropertyValue'),
        comtypes.STDMETHOD(HRESULT, 'SetDefaultEndpoint',
                           [ctypes.c_wchar_p, ctypes.c_uint]),
        comtypes.STDMETHOD(HRESULT, 'SetEndpointVisibility'),
    ]

_CPolicyConfigClient_CLSID = GUID('{870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}')

def _set_default_endpoint(endpoint_id: str):
    """Set Windows default render + communication device."""
    comtypes.CoInitializeEx(comtypes.COINIT_APARTMENTTHREADED)
    try:
        policy = comtypes.client.CreateObject(
            _CPolicyConfigClient_CLSID,
            interface=_IPolicyConfig,
            clsctx=CLSCTX_ALL,
        )
        # eRole: eConsole=0, eMultimedia=1, eCommunications=2
        for role in (0, 1, 2):
            try:
                policy.SetDefaultEndpoint(endpoint_id, role)
            except Exception:
                pass
    except Exception as e:
        print(f'[set_device error] {e}', file=sys.stderr, flush=True)
        return
    finally:
        comtypes.CoUninitialize()

    # After switching, refresh _epv to the new device and emit its volume
    threading.Thread(target=_reinit_volume_from_new_device, daemon=True).start()


def _reinit_volume_from_new_device():
    """Wait for Windows to apply the device switch, then re-read volume and update _epv."""
    import time
    time.sleep(0.45)          # Give Windows time to commit the default endpoint change
    comtypes.CoInitializeEx(comtypes.COINIT_APARTMENTTHREADED)
    try:
        speakers  = AudioUtilities.GetSpeakers()   # Now returns the NEW default device
        interface = speakers._dev.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        new_epv   = ctypes.cast(interface, ctypes.POINTER(IAudioEndpointVolume))
        vol  = round(new_epv.GetMasterVolumeLevelScalar() * 100)
        mute = bool(new_epv.GetMute())
        # Swap _epv so that future volume/mute commands target the right device
        with _lock:
            global _epv
            _epv = new_epv
        # Push updated volume to all Drawer/Taskbar windows
        emit_master(vol, mute)
        _k32.SetEvent(_event)
    except Exception as e:
        print(f'[device reinit error] {e}', file=sys.stderr, flush=True)
    finally:
        comtypes.CoUninitialize()


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

# ── Device enumeration (render-only via IMMDeviceEnumerator) ──────────────────

DEVICE_STATE_ACTIVE = 0x00000001
E_RENDER            = 0   # eRender
E_MULTIMEDIA        = 1   # eMultimedia role

def _devices_thread():
    comtypes.CoInitializeEx(comtypes.COINIT_APARTMENTTHREADED)
    try:
        enumerator  = AudioUtilities.GetDeviceEnumerator()
        collection  = enumerator.EnumAudioEndpoints(E_RENDER, DEVICE_STATE_ACTIVE)
        count       = collection.GetCount()

        try:
            default_dev = enumerator.GetDefaultAudioEndpoint(E_RENDER, E_MULTIMEDIA)
            active_id   = default_dev.GetId()
        except Exception:
            active_id = ''

        devices = []
        for i in range(count):
            try:
                dev    = collection.Item(i)
                dev_id = dev.GetId()
                # Use pycaw helper to get friendly name from IPropertyStore
                name = _get_device_name(dev) or dev_id
                devices.append({'id': dev_id, 'name': name})
            except Exception:
                pass

        print(json.dumps({'type': 'devices', 'devices': devices, 'activeId': active_id}),
              flush=True)
    except Exception as exc:
        print(json.dumps({'type': 'devices', 'devices': [], 'activeId': '', 'error': str(exc)}),
              flush=True)
    finally:
        comtypes.CoUninitialize()


def _get_device_name(dev) -> str:
    """Get friendly name via pycaw AudioDevice wrapper (handles IPropertyStore)."""
    try:
        all_devs = AudioUtilities.GetAllDevices()
        dev_id   = dev.GetId()
        for d in all_devs:
            if d.id == dev_id:
                return d.FriendlyName or ''
    except Exception:
        pass
    return ''

# ── Session helpers ────────────────────────────────────────────────────────────

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

        # Device list request
        elif cmd.get('cmd') == 'devices':
            threading.Thread(target=_devices_thread, daemon=True).start()

        # Set default render device
        elif 'set_device' in cmd:
            dev_id = str(cmd['set_device'])
            threading.Thread(target=_set_default_endpoint, args=(dev_id,), daemon=True).start()

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
