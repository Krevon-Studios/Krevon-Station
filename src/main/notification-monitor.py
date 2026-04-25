"""
notification-monitor.py
Reads Windows notification database (wpndatabase.db) every 1s.
Emits newline-delimited JSON to stdout. No extra dependencies.
"""
import sqlite3
import os
import json
import sys
import time
import gzip
import shutil
import tempfile
import winreg
import xml.etree.ElementTree as ET

DB_PATH = os.path.join(
    os.environ.get('LOCALAPPDATA', ''),
    'Microsoft', 'Windows', 'Notifications', 'wpndatabase.db'
)
TMP_DB = os.path.join(tempfile.gettempdir(), 'di_notif_snapshot.db')

def emit_err(msg: str):
    # Only emit critical errors, suppress routine diagnostic logs
    if "error:" in msg.lower() or "failed:" in msg.lower() or "not found" in msg.lower():
        sys.stderr.write(f'[notif:err] {msg}\n')
        sys.stderr.flush()

# ── DB open ───────────────────────────────────────────────────────────────────

def open_db():
    escaped = DB_PATH.replace('\\', '/')
    for uri in [f'file:{escaped}?immutable=1', f'file:{escaped}?mode=ro']:
        try:
            con = sqlite3.connect(uri, uri=True, timeout=1.0)
            con.row_factory = sqlite3.Row
            return con
        except Exception:
            pass
    try:
        shutil.copy2(DB_PATH, TMP_DB)
        con = sqlite3.connect(TMP_DB, timeout=1.0)
        con.row_factory = sqlite3.Row
        return con
    except Exception as e:
        emit_err(f'open failed: {e}')
        return None

def get_columns(con, table: str) -> list:
    try:
        return [r[1] for r in con.execute(f'PRAGMA table_info({table})').fetchall()]
    except Exception:
        return []

# ── Query builder ─────────────────────────────────────────────────────────────

def build_query(notif_cols: list, handler_cols: list) -> str:
    n_pk    = next((c for c in notif_cols if c.lower() in ('id','recordid','notificationid')), notif_cols[0])
    fk      = next((c for c in notif_cols if 'handler' in c.lower() and 'id' in c.lower()), None)
    h_pk    = next((c for c in handler_cols if 'record' in c.lower() or c.lower()=='id'), 'RecordId')
    app_col = next((c for c in handler_cols if 'primary' in c.lower() or 'aumid' in c.lower()), None)

    sel_app     = f'h.{app_col}' if app_col else 'NULL'
    sel_payload = 'n.PayloadData' if 'PayloadData' in notif_cols else 'NULL'
    sel_blob    = 'n.Payload'     if 'Payload'     in notif_cols else 'NULL'
    arrival     = 'n.ArrivalTime' if 'ArrivalTime' in notif_cols else '0'
    ptype       = 'n.PayloadType' if 'PayloadType' in notif_cols else 'NULL'

    join = (f'FROM Notification n LEFT JOIN NotificationHandler h ON n.{fk} = h.{h_pk}'
            if fk and app_col else 'FROM Notification n')

    return (
        f'SELECT n.{n_pk} as _id, {sel_payload} as _text, {sel_blob} as _blob, '
        f'{sel_app} as _app, {arrival} as _at, {ptype} as _ptype '
        f'{join} ORDER BY {arrival} DESC LIMIT 30'
    )

# ── Payload parsing ───────────────────────────────────────────────────────────

def find_xml(blob: bytes) -> str:
    """Search raw bytes for a <toast or <Toast XML fragment in UTF-8 or UTF-16 LE."""
    if not blob:
        return ''

    # UTF-8 search
    for marker in (b'<toast', b'<Toast'):
        idx = blob.find(marker)
        if idx >= 0:
            try:
                return blob[idx:].decode('utf-8', errors='replace')
            except Exception:
                pass

    # UTF-16 LE search  ('<' = 0x3C 0x00, 't' = 0x74 0x00 ...)
    for marker_u16 in (b'\x3c\x00\x74\x00\x6f\x00\x61\x00\x73\x00\x74\x00',   # <toast
                       b'\x3c\x00\x54\x00\x6f\x00\x61\x00\x73\x00\x74\x00'):  # <Toast
        idx = blob.find(marker_u16)
        if idx >= 0:
            try:
                raw = blob[idx:].decode('utf-16-le', errors='replace')
                # Strip embedded nulls that bleed through from padding
                return raw.replace('\x00', '')
            except Exception:
                pass

    return ''

def decode_blob(blob) -> str:
    if not blob or not isinstance(blob, (bytes, bytearray)):
        return ''
    b = bytes(blob)
    # Try gzip decompress first
    try:
        b = gzip.decompress(b)
    except Exception:
        pass
    # PayloadType=Xml → plain UTF-8 bytes, decode directly
    try:
        text = b.decode('utf-8', errors='replace')
        if '<' in text:
            return text
    except Exception:
        pass
    # Fallback: scan for XML markers (handles UTF-16 LE payloads)
    return find_xml(b)

def parse_payload(text_col, blob_col):
    """Return (title, body) for toast notifications only, or None to skip."""
    raw = ''
    if text_col and not isinstance(text_col, type(None)):
        raw = text_col if isinstance(text_col, str) else text_col.decode('utf-8', errors='replace')
        if '<' not in raw:
            raw = ''
    if not raw:
        raw = decode_blob(blob_col)
    if not raw:
        return None
    try:
        raw = raw[raw.index('<'):]
        # Skip XML comments / processing instructions before root element
        root = ET.fromstring(raw)
        tag = root.tag.split('}')[-1].lower()   # strip any namespace
        if tag != 'toast':
            return None                           # tile / badge — not a banner notification
        texts = root.findall('.//{*}text') or root.findall('.//text')
        title = (texts[0].text or '').strip() if len(texts) > 0 else ''
        body  = (texts[1].text or '').strip() if len(texts) > 1 else ''
        # Skip unresolved resource URIs (localized tile strings, not actual content)
        if title.startswith('ms-resource:') or body.startswith('ms-resource:'):
            return None
        return title, body
    except Exception:
        return None

# ── DND state ─────────────────────────────────────────────────────────────────

def get_dnd_state() -> bool:
    qs = (r'SOFTWARE\Microsoft\Windows\CurrentVersion\CloudStore\Store'
          r'\DefaultAccount\Current\default'
          r'\windows.data.notifications.quiethourssettings\Current')
    try:
        key  = winreg.OpenKey(winreg.HKEY_CURRENT_USER, qs)
        data, _ = winreg.QueryValueEx(key, 'Data')
        winreg.CloseKey(key)
        return isinstance(data, (bytes, bytearray)) and len(data) > 24
    except Exception:
        return False

def app_name_from_id(s: str) -> str:
    """Extract a human-readable app name from a Windows AUMID or path.

    UWP AUMID format:  Publisher.AppName_PackageHash!EntryPoint
      e.g.  5319275A.WhatsApp_cv1g1gvanyjgm!App  →  WhatsApp
            Microsoft.MicrosoftEdge_8wekyb3d8bbwe!MicrosoftEdge  →  MicrosoftEdge
    Win32 format:  C:\\path\\to\\app.exe  or just  AppName
    """
    if not s:
        return ''
    # Normalise slashes then split on '!'
    s = s.replace('\\', '/')
    parts = s.split('!')
    pkg = parts[0]          # everything before '!' – the package family name (UWP) or path (Win32)
    # For paths, take the filename only
    pkg = pkg.split('/')[-1]
    # UWP: Publisher.AppName_Hash  →  take last dot-segment, then strip _Hash
    if '.' in pkg:
        pkg = pkg.split('.')[-1]   # e.g. "WhatsApp_cv1g1gvanyjgm"
    pkg = pkg.split('_')[0]        # strip _Hash suffix  →  "WhatsApp"
    # Strip .exe extension (Win32)
    return pkg[:-4] if pkg.lower().endswith('.exe') else pkg

# ── Init ──────────────────────────────────────────────────────────────────────

if not os.path.exists(DB_PATH):
    emit_err(f'DB not found: {DB_PATH}')
    sys.exit(1)

emit_err(f'DB found: {DB_PATH}')

# Schema probe + payload format diagnostic
con0 = open_db()
if con0:
    nc = get_columns(con0, 'Notification')
    hc = get_columns(con0, 'NotificationHandler')
    emit_err(f'cols: {nc}')
    total = con0.execute('SELECT COUNT(*) FROM Notification').fetchone()[0]
    emit_err(f'rows: {total}')
    # Show first 3 rows for payload diagnosis
    try:
        sample = con0.execute(
            'SELECT Id, PayloadType, length(Payload) as plen, '
            'substr(Payload,1,64) as phex FROM Notification LIMIT 3'
        ).fetchall()
        for r in sample:
            blob = r['phex']
            hexstr = bytes(blob).hex() if blob and isinstance(blob, (bytes,bytearray)) else str(blob)
            emit_err(f'  row id={r["Id"]} ptype={r["PayloadType"]} plen={r["plen"]} hex={hexstr[:80]}')
    except Exception as e:
        emit_err(f'sample err: {e}')
    con0.close()

# Initial DND
dnd_now = get_dnd_state()
print(json.dumps({'type': 'dnd_state', 'enabled': dnd_now}))
sys.stdout.flush()

# ── Poll loop ─────────────────────────────────────────────────────────────────

query: str|None = None
last_dnd        = dnd_now
dnd_tick        = 0

# Start with an empty set so all notifications currently in the DB
# are emitted as 'added' on the first poll and shown immediately.
prev_ids: set = set()

while True:
    try:
        con = open_db()
        if con is None:
            time.sleep(2)
            continue

        if query is None:
            nc    = get_columns(con, 'Notification')
            hc    = get_columns(con, 'NotificationHandler')
            query = build_query(nc, hc)
            emit_err(f'query ready')

        rows = con.execute(query).fetchall()
        con.close()

        current_ids: set = set()
        for row in rows:
            rid = int(row['_id'])
            result = parse_payload(row['_text'], row['_blob'])
            if result is None:
                continue          # tile / badge / resource-string — skip
            current_ids.add(rid)
            if rid not in prev_ids:
                title, body = result
                app_id = row['_app'] or ''
                # ArrivalTime is a Windows FILETIME (100-ns ticks since 1601-01-01)
                # Convert to Unix milliseconds. Query aliases ArrivalTime as _at.
                raw_ts = int(row['_at']) if row['_at'] else 0
                if raw_ts > 10 ** 15:
                    arrival_ms = (raw_ts - 116444736000000000) // 10000
                elif raw_ts > 0:
                    arrival_ms = raw_ts * 1000          # assume Unix seconds
                else:
                    arrival_ms = int(time.time() * 1000)
                out = {
                    'type':        'added',
                    'id':          rid,
                    'appId':       app_id,
                    'appName':     app_name_from_id(app_id),
                    'title':       title,
                    'body':        body,
                    'arrivalTime': arrival_ms,
                }
                emit_err(f'emit id={rid} app={app_name_from_id(app_id)!r} title={title!r}')
                print(json.dumps(out, ensure_ascii=False))
                sys.stdout.flush()

        for rid in sorted(prev_ids - current_ids):
            print(json.dumps({'type': 'removed', 'id': rid}))
            sys.stdout.flush()

        prev_ids = current_ids

    except Exception as e:
        emit_err(f'poll error: {e}')
        query = None

    dnd_tick += 1
    if dnd_tick >= 5:
        dnd_tick = 0
        try:
            cur = get_dnd_state()
            if cur != last_dnd:
                last_dnd = cur
                print(json.dumps({'type': 'dnd_state', 'enabled': cur}))
                sys.stdout.flush()
        except Exception:
            pass

    time.sleep(1)
