# Krevon Station — Developer Reference

Internal docs for contributors and maintainers. For end-user info see [README.md](README.md).

---

## Project Structure

```
src/
├── main/
│   ├── index.ts              # App entry, IPC handlers
│   ├── window.ts             # BrowserWindow config (full-width, transparent, always-on-top)
│   ├── hook-server.ts        # Express server — receives Claude Code hooks
│   ├── media-watcher.ts      # Worker manager + PowerShell media control
│   ├── media-worker.ts       # Worker thread — SMTC session monitoring
│   ├── desktop-watcher.ts    # Virtual desktop state — registry monitor + switch IPC
│   ├── desktop-monitor.ps1   # PowerShell — blocks on RegNotifyChangeKeyValue, streams changes
│   ├── switch-desktop.ps1    # PowerShell — persistent stdin loop, calls native helper, falls back to hotkeys
│   ├── system-stats.ts       # Spawns Python monitors, broadcasts system-stats IPC
│   ├── auto-update.ts        # electron-updater wiring (GitHub Releases)
│   ├── appbar.ts             # SHAppBarMessage registration + AppBarHelper.exe runner
│   ├── audio-monitor.py      # Python — IAudioEndpointVolumeCallback COM callback (zero polling)
│   ├── network-monitor.py    # Python — Hybrid: NotifyAddrChange + polling for signal strength
│   ├── notification-monitor.py # Python — Windows Runtime toast notification watcher
│   ├── wifi-scan.py          # Python — Native wlanapi.dll async network scanner
│   ├── wifi-toggle.py        # Python — Native wlanapi.dll radio toggle (no admin required)
│   ├── vendor/
│   │   ├── VirtualDesktopHelper.exe
│   │   ├── VirtualDesktopHelper.source.cs
│   │   ├── VirtualDesktopHelper.LICENSE.txt
│   │   ├── AppBarHelper.exe
│   │   └── AppBarHelper.source.cs
│   └── tray.ts               # System tray menu
├── preload/
│   └── index.ts              # Context-isolated window.island IPC bridge
└── renderer/src/
    ├── types.ts              # IslandState, MediaSessionData
    ├── env.d.ts              # window.island API types (incl. SystemStats)
    ├── components/
    │   ├── Island.tsx        # All state UIs + media controls
    │   ├── Taskbar.tsx       # Full-width top bar — desktop dots + live system icons
    │   ├── Drawer.tsx        # Framer Motion animated WiFi & Audio control panel
    │   └── NotificationCards.tsx  # Live Windows notification panel with scroll & dismiss
    └── store/
        └── useIslandStore.ts # State machine + window resize logic

scripts/
├── compile-python.ps1        # PyInstaller — compiles .py helpers → resources/python/*.exe
└── build-python.ps1          # Alternative: downloads embeddable Python 3.14 + installs deps

build/
├── installer.nsh             # Custom NSIS script — startup registry key
└── pyinstaller/              # PyInstaller work/cache (gitignored)

resources/
├── logo_solid.png            # App icon (taskbar, installer)
├── logo_transparent.png      # Tray icon
└── python/                   # Compiled Python helper .exe files (committed, shipped via extraResources)
```

---

## Dev Environment Setup

**Prerequisites:**
- Windows 10/11
- [Bun](https://bun.sh) — `winget install Oven-sh.Bun`
- Node.js 18+ (Electron toolchain)
- Python 3.14 with pip

**Install Python build deps** (needed to compile helpers, not needed at runtime):
```powershell
pip install pycaw psutil pyinstaller `
  winrt-Windows.Foundation winrt-Windows.Foundation.Collections `
  winrt-Windows.UI.Notifications winrt-Windows.UI.Notifications.Management
```

**Install JS deps:**
```bash
bun install
```

**Run in dev mode:**
```bash
bun run dev
```

Dev mode spawns raw `.py` scripts directly via `py`. Python + packages must be on PATH.

---

## Build Pipeline

### 1. Compile Python helpers
Run whenever `.py` files change:
```powershell
powershell -ExecutionPolicy Bypass -File scripts\compile-python.ps1
```

Outputs `resources/python/audio-monitor.exe`, `network-monitor.exe`, `notification-monitor.exe`, `wifi-scan.exe`, `wifi-toggle.exe`.

Commit these — they are bundled by `electron-builder` via `extraResources`.

**Why `--collect-all` flags matter:** `pycaw` uses `comtypes` which generates COM type-library wrappers at build time. Without `--collect-all=comtypes`, the stubs are missing on other machines and all COM callbacks fail silently. Same logic applies to `psutil` (binary extensions) and `winrt` (namespace packages).

### 2. Build installer
```bash
bun run build   # compile TS + React → out/
bun run start   # optional: preview packaged build locally
bun run dist    # package → dist/Krevon Station Setup <version>.exe
```

### 3. Release a new version
1. Bump `"version"` in `package.json`
2. Set `owner` in `electron-builder.yml` → `publish` block to your GitHub username
3. `$env:GH_TOKEN = "ghp_..."`
4. `bun run dist` — uploads installer + `latest.yml` to GitHub Releases

---

## How It Works

### Claude Code Flow

```
Claude Code                Hook Server            Renderer
    │                          │                      │
    ├─ SessionStart ──curl.exe─▶ POST /session         │
    │                          ├── island:session-start▶ pill expands
    │                          │                      │
    ├─ PreToolUse ───curl.exe─▶ POST /tool             │
    │                          ├── island:tool-active ▶ shows tool name
    │                          │                      │
    └─ Stop ─────────curl.exe─▶ POST /done             │
                               └── island:task-done  ▶ shows cost + turns
```

### Media Control Flow

```
User clicks button in pill
    │
    ▼
window.island.controlMedia(action, sourceAppId)
    │
    ▼ IPC: control-media
Main process
    │
    ▼
PowerShell script (di-control.ps1 in %TEMP%)
    │
    ├─ 1. WinRT TryTogglePlayPauseAsync() on specific SMTC session
    │      → works for UWP apps (Spotify, etc.)
    │      → polls IAsyncInfo.Status until complete
    │
    └─ 2. PostMessage(WM_APPCOMMAND) to app window
           → fallback for Win32 apps (Chrome, Edge, Firefox)
```

### Media Monitoring

Worker thread runs `@coooookies/windows-smtc-monitor` (NAPI native addon, ABI-stable). Fires events on any SMTC change → sends full sessions array to main → forwards to renderer via `island:media` IPC. Starts 1.5s after app ready to avoid blocking main thread startup.

Multi-source tracking uses `sourceAppId` (not array index) to prevent UI jumps when Windows reorders sessions.

### Notification System

```
notification-monitor.exe (persistent — STA thread, COM apartment)
    │
    ├─ UserNotificationListener.request_access_async()  [WinRT]
    │      → requires Settings > Privacy > Notifications access
    │
    ├─ Initial snapshot: get_notifications_async(TOAST)
    │
    ├─ add_notification_changed() event subscription  [best-effort]
    │      → falls back to 2 s poll if event registration fails
    │
    ├─ stdin: {"cmd": "remove", "ids": [...]}
    │      → asyncio.run_coroutine_threadsafe → STA loop
    │      → remove_notification(id) per ID
    │      → _removed_ids set prevents duplicate removed events
    │
    └─ stdout JSON lines → readline → sendToNotifWin() → island:notifications
```

### Virtual Desktop Pagination

```
App start
    │
    ├─ readInitialState() ── reg query ──▶ parse VirtualDesktopIDs + CurrentVirtualDesktop
    │
    ├─ desktop-monitor.ps1 ── RegNotifyChangeKeyValue ──▶ blocks until registry changes
    │      (persistent)  └─ stdout "idsHex|currentHex" ──▶ island:virtual-desktops
    │
    └─ switch-desktop.ps1 ── stdin loop ──▶ reads "targetIndex|fromIndex"
           (persistent)     ├─ VirtualDesktopHelper.exe /Animation:Off /Switch:<n>
                            └─ fallback: keybd_event(Win+Ctrl+Arrow × N steps)
```

Optimistic IPC push on switch → instant visual feedback → registry monitor confirms.

### App Bar Registration

```
App start
    │
    ├─ AppBarHelper.exe
    │      ├─ SetProcessDpiAwarenessContext(PerMonitorV2)
    │      ├─ GetDpiForMonitor(primary) → convert 32 DIP → physical px
    │      └─ SHAppBarMessage(ABM_SETPOS, edge=top)
    │             → shell reserves top strip; maximised apps respect it
    │
    └─ stdout "left|top|right|bottom" (physical px)
           → appbar.ts divides by scaleFactor → DIPs → taskbarWin.setBounds()
```

---

## IPC Channels

| Channel | Direction | Payload |
|---|---|---|
| `island:session-start` | Main → Renderer | `{ session_id }` |
| `island:tool-active` | Main → Renderer | `{ tool_name }` |
| `island:task-done` | Main → Renderer | `{ total_cost_usd, num_turns, duration_ms }` |
| `island:media` | Main → Renderer | `{ sessions: MediaSessionData[] }` |
| `island:hover` | Main → Renderer | `boolean` |
| `island:virtual-desktops` | Main → Renderer | `{ count: number, activeIndex: number }` |
| `island:notifications` | Main → notifWin | `{ type, id, appId, appName, title, body, arrivalTime }` |
| `system-stats` | Main → Renderer | `{ network: NetworkState, audio: AudioState }` |
| `accent-color` | Main → All windows | `{ r, g, b }` |
| `drawer:show` | Main → drawerWin + notifWin | `type: string` |
| `drawer:force-close` | Main → drawerWin + notifWin | — |
| `drawer:height` | Main → notifWin | `h: number` |
| `drawer:resize` | notifWin → Main | `h: number` |
| `get-virtual-desktops` | Renderer → Main (invoke) | — returns `{ count, activeIndex }` |
| `get-system-stats` | Renderer → Main (invoke) | — returns `SystemStats` snapshot |
| `scan-wifi-networks` | Renderer → Main (invoke) | — returns `WifiNetwork[]` |
| `get-wifi-state` | Renderer → Main (invoke) | — returns `{ enabled: boolean }` |
| `set-wifi-enabled` | Renderer → Main (invoke) | `enable: boolean` |
| `connect-wifi` | Renderer → Main (invoke) | `ssid: string` |
| `get-notif-icon` | notifWin → Main (invoke) | `appId: string` — returns base64 PNG or null |
| `dismiss-notifications` | notifWin → Main (invoke) | `ids: number[]` |
| `set-session-volume` | Renderer → Main | `(pid, volume, muted)` |
| `set-audio-device` | Renderer → Main | `deviceId: string` |
| `switch-virtual-desktop` | Renderer → Main | `targetIndex: number` |
| `control-media` | Renderer → Main | `(action, sourceAppId)` |
| `set-hit-box` | Renderer → Main | `(w, h)` |
| `get-user-info` | Renderer → Main (invoke) | — returns `{ avatar: string\|null, name: string }` |
| `system-action` | Renderer → Main (invoke) | `action: 'lock'\|'sleep'\|'restart'\|'shutdown'\|'screenshot'\|'settings'\|'profile'\|'launch:<appId>'` |

---

## Window Architecture

| Window | Role | Focusable | Always visible |
|---|---|---|---|
| `taskbarWin` | Full-width top bar | No | Yes |
| `islandWin` | Floating pill | No | Yes |
| `drawerWin` | WiFi/audio drawer | Yes (for blur) | No (shown on demand) |
| `notifWin` | Notification overlay | No | Yes (click-through) |

**Hit-test polling:** Main process polls `screen.getCursorScreenPoint()` at 16ms. Calls `setIgnoreMouseEvents()` each tick — no delta guard — self-heals after lock/sleep/fullscreen. DWM cache invalidation uses lazy 1px resize on first hover after reassert.

**Registered App Bar:** `taskbarWin` registered via `SHAppBarMessage(ABM_SETPOS)` so Windows shell reserves the strip. Survives display-metrics changes.

---

## Stack

| Layer | Tech |
|---|---|
| App shell | Electron 31 |
| Build | electron-vite + Vite 5 |
| UI | React 18 + Tailwind CSS + Framer Motion |
| Icons | Lucide React |
| Package manager | Bun |
| Media monitoring | `@coooookies/windows-smtc-monitor` (NAPI native) |
| Hook server | Express on `127.0.0.1:7823` |
| System stats | Python 3.14 + `pycaw` + `psutil` + `wlanapi.dll` |
| Notifications | Python 3.14 + `winrt` (`UserNotificationListener`) |
| Auto-update | `electron-updater` via GitHub Releases |
