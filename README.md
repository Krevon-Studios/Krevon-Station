# Dynamic Island for Windows

A macOS-style Dynamic Island overlay for Windows, built with Electron + React. Shows real-time Claude Code activity (tool calls, session start, task completion) and Windows media controls — all in a floating pill at the top of your screen.

![idle](https://img.shields.io/badge/state-idle-555) ![tool](https://img.shields.io/badge/state-tool__active-7C6AFF) ![done](https://img.shields.io/badge/state-task__done-34D399) ![media](https://img.shields.io/badge/state-media-1DB954)

---

## Features

- **Claude Code integration** — live tool call display, session start notification, task completion with cost/turns/duration
- **Windows media controls** — play/pause, skip forward/back, directly from the pill
- **Multi-source support** — cycle between Spotify, Chrome, Edge, etc. with per-session control
- **Click-through** — mouse passes through the pill when not hovering; interactive on hover
- **Dynamic resize** — pill expands/contracts smoothly per state
- **Always on top** — hides behind fullscreen apps automatically
- **System tray** — show/hide/quit from tray icon

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

---

## States

| Mode | Trigger | Size | Duration |
|---|---|---|---|
| `idle` | Default / no activity | 173×52 | — |
| `session_start` | Claude Code session begins | 223×62 | 2.5s then idle/media |
| `tool_active` | Claude Code tool call | 283×70 | Until Stop hook |
| `task_done` | Claude Code task completes | 318×70 | 5s then idle/media |
| `media` | SMTC media session active | 368×74 | While session exists |

Claude Code states always override media state. When Claude finishes, media resumes if something is playing.

---

## Project Structure

```
src/
├── main/
│   ├── index.ts          # App entry, IPC handlers
│   ├── window.ts         # BrowserWindow config (transparent, always-on-top)
│   ├── hook-server.ts    # Express server — receives Claude Code hooks
│   ├── media-watcher.ts  # Worker manager + PowerShell media control
│   ├── media-worker.ts   # Worker thread — SMTC session monitoring
│   └── tray.ts           # System tray menu
├── preload/
│   └── index.ts          # Context-isolated window.island IPC bridge
└── renderer/src/
    ├── types.ts           # IslandState, MediaSessionData
    ├── env.d.ts           # window.island API types
    ├── components/
    │   └── Island.tsx     # All state UIs + media controls
    └── store/
        └── useIslandStore.ts  # State machine + window resize logic
```

---

## Setup

### Prerequisites

- Windows 10/11
- [Bun](https://bun.sh) — `winget install Oven-sh.Bun`
- [Node.js](https://nodejs.org) 18+ (for Electron toolchain)

### Install & Run

```bash
git clone https://github.com/yourusername/dynamic-island
cd dynamic-island
bun install
bun run dev
```

### Build

```bash
bun run build
bun run start   # preview production build
```

---

## Claude Code Hooks

Add to `C:\Users\<you>\.claude\settings.json`:

```json
{
  "hooks": {
    "SessionStart": [{
      "hooks": [{
        "type": "command",
        "command": "curl.exe -s -X POST http://127.0.0.1:7823/session -H \"Content-Type: application/json\" -d @-"
      }]
    }],
    "PreToolUse": [{
      "hooks": [{
        "type": "command",
        "command": "curl.exe -s -X POST http://127.0.0.1:7823/tool -H \"Content-Type: application/json\" -d @-"
      }]
    }],
    "Stop": [{
      "hooks": [{
        "type": "command",
        "command": "curl.exe -s -X POST http://127.0.0.1:7823/done -H \"Content-Type: application/json\" -d @-"
      }]
    }]
  }
}
```

> **Note:** Use `curl.exe` not `curl` — PowerShell aliases `curl` to `Invoke-WebRequest` which has different syntax.

Start the Dynamic Island app **before** starting a Claude Code session. The hook server listens on `http://127.0.0.1:7823`.

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

---

## IPC Channels

| Channel | Direction | Payload |
|---|---|---|
| `island:session-start` | Main → Renderer | `{ session_id }` |
| `island:tool-active` | Main → Renderer | `{ tool_name }` |
| `island:task-done` | Main → Renderer | `{ total_cost_usd, num_turns, duration_ms }` |
| `island:media` | Main → Renderer | `{ sessions: MediaSessionData[] }` |
| `set-ignore-mouse` | Renderer → Main | `boolean` |
| `control-media` | Renderer → Main | `(action, sourceAppId)` |
| `set-window-size` | Renderer → Main | `(w, h)` |

---

## Media Source Support

Recognized apps (from SMTC `SourceAppUserModelId`):

| App | AUMID pattern |
|---|---|
| Spotify | `SpotifyAB.SpotifyMusic_...!Spotify` |
| Chrome | `Chrome` |
| Firefox | `Firefox` |
| Edge | `msedge` / `Microsoft.Edge` |
| Opera | `opera` |
| Brave | `brave` |
| VLC | `vlc` |
| Amazon Music | `amazon` |
| YT Music | `youtubemusic` |
| TIDAL | `tidal` |
| foobar2000 | `foobar2000` |

Unknown apps fall back to the last segment of the AUMID.

---

## Tool Label Mapping

| Tool name (contains) | Display label |
|---|---|
| `read` | Reading file |
| `write` | Writing file |
| `edit` | Editing file |
| `bash` | Running command |
| `glob` | Searching files |
| `grep` | Searching code |
| `web_search` | Searching web |
| `web_fetch` | Fetching URL |
| `todo` | Updating tasks |
| `agent` | Spawning agent |

Unknown tools are title-cased from their snake_case name.

---

## Window Behavior

- **Transparent** — `transparent: true`, `backgroundColor: '#00000000'`, no frame
- **No shadow** from OS — shadow drawn via CSS `inset` box-shadow only (prevents dark glow on transparent bg)
- **Position** — centered horizontally, `y: 6` from top edge of primary display
- **Pill shape** — `border-radius: 999px` on inner div; window has 4px padding on each side for clean AA
- **Always on top** — `setAlwaysOnTop(true, 'normal')` hides behind fullscreen apps
- **Non-focusable** — `focusable: false`; keyboard focus never stolen
- **Click-through** — `setIgnoreMouseEvents(true, { forward: true })` by default; disabled while cursor is over pill

---

## Troubleshooting

**Pill not appearing**
- Check `bun run dev` output for errors
- Ensure no other instance is running (single-instance lock)

**Claude hooks not firing**
- Verify `curl.exe` (not `curl`) in settings.json
- App must be running before Claude Code session starts
- Test: `curl.exe -X POST http://127.0.0.1:7823/session -H "Content-Type: application/json" -d "{}"`

**Media not showing**
- Requires Windows 10 1809+ for SMTC API
- Check console for `[media-watcher] ready` log
- Play something in Spotify or a browser — appears within 1.5s of app start

**Media controls wrong session**
- WinRT control targets session by `SourceAppUserModelId`; select correct source using `<` `>` arrows in pill
- Controls take ~300–800ms (PowerShell startup + WinRT init)

---

## License

MIT
