"""
notification-monitor.py
=======================
Uses the official Windows Runtime API:
  Windows.UI.Notifications.Management.UserNotificationListener

Protocol (stdout, newline-delimited JSON):
  {"type": "added",   "id": <int>, "appId": "<str>", "appName": "<str>",
   "title": "<str>", "body": "<str>", "arrivalTime": <unix_ms>}
  {"type": "removed", "id": <int>}

Stdin commands (from Electron main process):
  {"cmd": "remove", "ids": [<int>, ...]}  → removes specific notifications

Requirements:
  pip install winrt-Windows.Foundation winrt-Windows.Foundation.Collections
              winrt-Windows.UI.Notifications winrt-Windows.UI.Notifications.Management
"""

import asyncio
import ctypes
import io
import json
import sys
import threading
import time
import winreg

# Force line-buffered (unbuffered for binary) output so every emit() reaches
# Electron's readline pipe immediately, even when stdout is not a tty.
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', line_buffering=True)
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', line_buffering=True)

# ── COM STA setup ──────────────────────────────────────────────────────────────
# WinRT event registration requires an STA (Single-Threaded Apartment) thread.
COINIT_APARTMENTTHREADED = 0x2

# ── Shared state ───────────────────────────────────────────────────────────────
# Set by _main_impl() once the listener is ready; read by _stdin_reader().
_listener_ref: object = None
_sta_loop: asyncio.AbstractEventLoop | None = None
# Set of IDs already emitted as removed via _do_remove — lets snapshot() skip
# re-emitting a duplicate removed event for the same ID.
_removed_ids: set[int] = set()

# ── App identity ───────────────────────────────────────────────────────────────
APP_ID = "DynamicIsland.NotificationListener"


def register_aumid() -> None:
    key_path = rf"SOFTWARE\Classes\AppUserModelId\{APP_ID}"
    try:
        key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, key_path)
        winreg.SetValueEx(key, "DisplayName", 0, winreg.REG_SZ, "Dynamic Island")
        winreg.SetValueEx(key, "IconUri",     0, winreg.REG_SZ, "")
        winreg.CloseKey(key)
    except Exception as e:
        emit_err(f"registry write: {e}")
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)
    except Exception as e:
        emit_err(f"SetCurrentProcessExplicitAppUserModelID: {e}")


# ── Helpers ────────────────────────────────────────────────────────────────────

def emit(obj: dict) -> None:
    print(json.dumps(obj, ensure_ascii=False))
    sys.stdout.flush()


def emit_err(msg: str) -> None:
    sys.stderr.write(f"[notif:err] {msg}\n")
    sys.stderr.flush()


# ── Stdin command reader ───────────────────────────────────────────────────────
# Runs on the Python main thread (blocking).
# Marshals remove_notification() back onto the STA asyncio loop via
# run_coroutine_threadsafe so the COM call happens on the correct apartment.

def _stdin_reader() -> None:
    # On Windows, stdin from an Electron pipe may arrive in binary chunks.
    # Wrap it in a text reader with explicit \n line splitting.
    try:
        stdin = io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8", newline="\n")
    except AttributeError:
        stdin = sys.stdin  # fallback for environments without .buffer

    for raw_line in stdin:
        raw_line = raw_line.strip()
        if not raw_line:
            continue
        try:
            cmd = json.loads(raw_line)
        except Exception:
            continue

        if cmd.get("cmd") == "remove":
            ids = [int(x) for x in cmd.get("ids", []) if x is not None]
            if not ids or _listener_ref is None or _sta_loop is None:
                continue

            # Marshal the removal onto the STA asyncio loop — COM objects must
            # be called from the apartment thread that created them.
            # Capture loop reference NOW (module-level, set before listener_ref)
            loop_ref = _sta_loop
            listener_ref = _listener_ref

            async def _do_remove(ids: list = ids, _listener=listener_ref) -> None:
                for nid in ids:
                    try:
                        _listener.remove_notification(nid)  # type: ignore[union-attr]
                        # Emit removed event immediately — do NOT rely on the
                        # notification_changed event or the 2-second poll fallback.
                        # The changed-event subscription can fail silently (swallowed
                        # except at line ~223), leaving the UI stuck with stale state.
                        _removed_ids.add(nid)
                        emit({"type": "removed", "id": nid})
                    except Exception as e:
                        emit_err(f"remove_notification({nid}): {e}")
                        # Still emit removed so the UI stays consistent even if
                        # the WinRT call failed (e.g. notification already gone).
                        _removed_ids.add(nid)
                        emit({"type": "removed", "id": nid})

            try:
                asyncio.run_coroutine_threadsafe(_do_remove(), loop_ref)
            except Exception as e:
                emit_err(f"schedule remove: {e}")


# ── Main async implementation (runs on STA thread) ────────────────────────────

async def _main_impl() -> None:
    global _listener_ref, _sta_loop
    _sta_loop = asyncio.get_running_loop()

    register_aumid()

    try:
        from winrt.windows.ui.notifications.management import (
            UserNotificationListener,
            UserNotificationListenerAccessStatus,
        )
        from winrt.windows.ui.notifications import (
            NotificationKinds,
            KnownNotificationBindings,
        )
    except ImportError as e:
        emit_err(
            f"Missing winrt packages: {e}  |  "
            "Run: pip install winrt-Windows.Foundation winrt-Windows.Foundation.Collections "
            "winrt-Windows.UI.Notifications winrt-Windows.UI.Notifications.Management"
        )
        sys.exit(1)

    listener = UserNotificationListener.current
    status   = await listener.request_access_async()

    if status != UserNotificationListenerAccessStatus.ALLOWED:
        emit_err(
            f"Access denied (status={status}). "
            "Open Settings > Privacy & security > Notifications and "
            "enable 'Allow apps to access your notifications', then restart."
        )
        sys.exit(1)

    # Expose listener to stdin reader — set AFTER access is confirmed
    _listener_ref = listener

    prev_ids: set[int] = set()
    loop             = asyncio.get_running_loop()
    toast_key        = KnownNotificationBindings.toast_generic

    # ── Snapshot: diff Action Center against known state ───────────────────────
    async def snapshot() -> None:
        nonlocal prev_ids
        try:
            notifs = await listener.get_notifications_async(NotificationKinds.TOAST)
        except Exception as e:
            emit_err(f"get_notifications_async: {e}")
            return

        current_ids: set[int] = set()

        for n in notifs:
            try:
                nid = int(n.id)
                current_ids.add(nid)
                if nid in prev_ids:
                    continue  # already reported

                app_id = app_name = ""
                try:
                    if n.app_info:
                        app_id = n.app_info.app_user_model_id or ""
                        if n.app_info.display_info:
                            app_name = n.app_info.display_info.display_name or ""
                except Exception:
                    pass

                title = body = ""
                try:
                    binding = n.notification.visual.get_binding(toast_key)
                    if binding:
                        texts = [t.text for t in binding.get_text_elements() if t.text]
                        title = texts[0] if texts else ""
                        body  = texts[1] if len(texts) > 1 else ""
                except Exception as ex:
                    emit_err(f"parse id={nid}: {ex}")

                emit({
                    "type":        "added",
                    "id":          nid,
                    "appId":       app_id,
                    "appName":     app_name,
                    "title":       title,
                    "body":        body,
                    "arrivalTime": int(time.time() * 1000),
                })
            except Exception as ex:
                emit_err(f"notification iter: {ex}")

        # Only emit removed for IDs that snapshot() hasn't already seen removed
        # via an explicit _do_remove call (which emits immediately). This prevents
        # a duplicate removed event arriving at the renderer for the same ID.
        stale = (prev_ids - current_ids) - _removed_ids
        for rid in sorted(stale):
            emit({"type": "removed", "id": rid})

        # Clean up _removed_ids entries that are no longer in prev_ids anyway
        _removed_ids.difference_update(prev_ids - current_ids)
        prev_ids = current_ids

    # ── Initial snapshot ───────────────────────────────────────────────────────
    await snapshot()

    # ── Real-time event subscription (best-effort; needs STA COM marshaling) ───
    event_ok = False
    try:
        def _on_changed(sender, args):
            loop.call_soon_threadsafe(lambda: asyncio.ensure_future(snapshot()))
        listener.add_notification_changed(_on_changed)
        event_ok = True
    except Exception:
        pass  # silently fall back to polling

    # ── Keep alive ─────────────────────────────────────────────────────────────
    if event_ok:
        while True:
            await asyncio.sleep(3600)   # events drive updates
    else:
        while True:
            await asyncio.sleep(2)      # 2-second poll fallback
            await snapshot()


# ── STA thread runner ──────────────────────────────────────────────────────────

def _run_on_sta_thread() -> None:
    try:
        ctypes.windll.ole32.CoInitializeEx(None, COINIT_APARTMENTTHREADED)
    except Exception:
        pass
    try:
        asyncio.run(_main_impl())
    except KeyboardInterrupt:
        pass
    except Exception as e:
        emit_err(f"fatal: {e}")
    finally:
        try:
            ctypes.windll.ole32.CoUninitialize()
        except Exception:
            pass


# ── Entry point ────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    # 1. Start the STA thread (asyncio + WinRT)
    sta_thread = threading.Thread(target=_run_on_sta_thread, daemon=True)
    sta_thread.start()

    # 2. Wait until the listener is ready before accepting stdin commands
    while _listener_ref is None and sta_thread.is_alive():
        time.sleep(0.05)

    # 3. Block the main thread reading stdin commands
    if sta_thread.is_alive():
        try:
            _stdin_reader()
        except KeyboardInterrupt:
            pass

    try:
        sta_thread.join()
    except KeyboardInterrupt:
        pass
