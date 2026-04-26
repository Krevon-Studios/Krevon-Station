# Krevon Station

> A macOS-style Dynamic Island for Windows — floating pill at the top of your screen with live Claude Code activity, media controls, WiFi & audio management, virtual desktop switching, and Windows notifications.

![idle](https://img.shields.io/badge/state-idle-555)
![tool](https://img.shields.io/badge/state-tool__active-7C6AFF)
![done](https://img.shields.io/badge/state-task__done-34D399)
![media](https://img.shields.io/badge/state-media-1DB954)
![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4)
![license](https://img.shields.io/badge/license-MIT-green)

---

## What is it?

Krevon Station puts a smart, always-on-top pill at the top of your screen — similar to the Dynamic Island on iPhone. It stays out of your way (fully click-through) until it has something to show you, then smoothly expands with live information.

It also replaces your system taskbar strip with a sleek full-width bar that shows virtual desktop dots, system tray icons, WiFi signal, and volume — all reactive to your system in real time.

---

## Features

### Claude Code Integration
- Shows which tool Claude is currently running (Reading file, Writing file, Running command, etc.)
- Displays session start notifications
- Task completion summary with total cost, turn count, and duration

### Media Controls
- Play/pause, skip forward, skip back directly from the pill
- Supports Spotify, Chrome, Edge, Firefox, VLC, and more
- Multiple sources active at once? Cycle between them with pagination dots
- Album art, track name, and artist shown on hover

### Live System Info
- **WiFi** — signal strength (4 levels), SSID, no-network and no-internet badges
- **Audio** — master volume level and mute state, with a 4-level icon
- **VPN** — key icon appears automatically when a VPN adapter is detected
- **Windows accent color** — all UI accents follow your color set in Settings → Personalization → Colors, live without restart

### Control Drawer
Click any system tray icon to open an animated panel with:
- Live WiFi network scanner — scan, see signal strength, connect
- Real-time per-app audio mixer — adjust volume per application, switch output devices

### Windows Notifications
- Live toast notifications appear in a scrollable panel below the drawer
- App icon, name, title, body, and arrival time
- Dismiss individually, clear all, or click to launch the source app

### Virtual Desktop Pagination
- Full-width taskbar shows your desktop count and active index as dots
- Click a dot to jump directly to that desktop (no stepping through intermediate ones on supported builds)
- Falls back to Win+Ctrl+Arrow on unsupported systems

---

## Installation

1. Go to the [**Releases**](../../releases) page
2. Download `Krevon Station Setup x.x.x.exe`
3. Run the installer — no admin rights required
4. Krevon Station starts immediately and runs in the background on every login

> **SmartScreen warning:** Because the app is unsigned, Windows may show "Windows protected your PC" on first run. Click **More info → Run anyway**.

---

## Claude Code Setup

Add these hooks to `C:\Users\<you>\.claude\settings.json` so the pill reacts to your Claude Code sessions:

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

> Use `curl.exe` not `curl` — PowerShell aliases `curl` to `Invoke-WebRequest`.

Start Krevon Station **before** opening a Claude Code session. The hook server listens on `http://127.0.0.1:7823`.

---

## Notification Access

For the notification panel to work, grant access once:

**Settings → Privacy & security → Notifications → Allow apps to access your notifications → On**

---

## Auto-Update

Krevon Station checks for updates silently in the background on every launch. When a new version is ready, you'll get a prompt to restart. You can also trigger a manual check from the system tray icon → **Check for updates…**

---

## Troubleshooting

**Pill not appearing**
- Make sure no other instance is already running (check system tray)
- Run from the Start Menu shortcut, not the installer

**Claude hooks not firing**
- Confirm `curl.exe` (not `curl`) in `settings.json`
- Krevon Station must be running before you start Claude Code
- Quick test: `curl.exe -X POST http://127.0.0.1:7823/session -H "Content-Type: application/json" -d "{}"`

**Media not showing**
- Requires Windows 10 1809 or later
- Play something in Spotify or a browser — it appears within ~1.5 seconds

**WiFi/audio not showing**
- These features are self-contained — no Python install required on your machine
- If icons are stuck, check the system tray → right-click → Quit, then relaunch

**Notifications not appearing**
- Open the drawer first (click a system tray icon) — the notification panel only shows while the drawer is open
- Check that notification access is granted (see above)
- Confirm there are unread notifications in Action Center

**Virtual desktop dots not switching**
- Direct switching targets Windows 11 24H2 — on other builds it falls back to Win+Ctrl+Arrow (works everywhere, just animates through intermediate desktops)

---

## Supported Media Apps

Spotify, Chrome, Edge, Firefox, Opera, Brave, VLC, Amazon Music, YouTube Music, TIDAL, foobar2000, WhatsApp — and any other app that registers with the Windows SMTC API.

---

## Contributing

See [DEVELOPMENT.md](DEVELOPMENT.md) for project structure, dev setup, build pipeline, IPC channel reference, and architecture notes.

---

## License

MIT
