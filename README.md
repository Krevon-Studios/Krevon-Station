# Krevon Station

> A macOS-style Dynamic Island for Windows — floating pill at the top of your screen with media controls, Wi-Fi & Bluetooth management, audio mixing, virtual desktop switching, and Windows notifications.

![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4)
![license](https://img.shields.io/badge/license-MIT-green)
![version](https://img.shields.io/badge/version-1.1.1-blue)

---

## What is it?

Krevon Station puts a smart, always-on-top pill at the top of your screen — similar to the Dynamic Island on iPhone. It stays out of your way until it has something to show you, then smoothly expands with live information.

It also replaces your system taskbar strip with a sleek full-width bar showing virtual desktop dots, Wi-Fi signal, Bluetooth state, and volume — all reactive to your system in real time.

Built entirely from scratch in C++ using Win32, Direct2D, and DirectComposition. No frameworks, no Electron, no runtime dependencies.

---

## Features

### Dynamic Island
- Shows the current time when idle
- Switches to media title + animated visualizer when something is playing
- Hover to expand — see album art, track, artist, playback controls, and a seekable progress slider
- Multiple media sources active at once? Cycle between them with pagination dots
- Supports Spotify, Chrome, Edge, Firefox, Zen Browser, YouTube Music Desktop App, VLC, and any app that reports to the Windows SMTC API

### Quick Settings Drawer
Click the status cluster (Wi-Fi / Bluetooth / volume icons) to open an animated panel:
- **Wi-Fi** — scan networks, see signal strength, connect to saved or new networks
- **Bluetooth** — toggle radio, see paired devices, connect / disconnect
- **Audio** — master volume slider, per-app volume mixer, output device switcher
- **Power** — sleep, restart, shut down

### Windows Notifications
- Live toast notifications in a scrollable panel below the drawer
- App icon, name, title, body, and arrival time
- Dismiss individually or click to launch the source app

### Virtual Desktop Pager
- Full-width taskbar shows your desktop count and active index as dots
- Click a dot to jump directly to that desktop

### System Tray
- Right-click → **Check for updates…** or **Quit**
- Silent background update check on every launch

---

## Installation

1. Go to the [**Releases**](../../releases) page
2. Download `Krevon Station Setup x.x.x.exe`
3. Run the installer — no admin rights required
4. Krevon Station starts immediately and runs in the background on every login

> **SmartScreen warning:** Because the installer is unsigned, Windows may show "Windows protected your PC" on first run. Click **More info → Run anyway**. This is expected for open-source apps without an EV certificate.

---

## Notification Access

For the notification panel to work, grant access once:

**Settings → Privacy & security → Notifications → Allow apps to access your notifications → On**

---

## Auto-Update

Krevon Station checks for updates silently on every launch. When a newer release is available on GitHub, it downloads the installer and prompts you to restart. You can also trigger a manual check from the system tray → **Check for updates…**

---

## Requirements

- Windows 10 (1809+) or Windows 11, x64
- No additional runtime or dependencies required

---

## Troubleshooting

**Pill not appearing**
- Make sure no other instance is running (check system tray)
- Launch from the Start Menu shortcut

**Media not showing**
- Requires Windows 10 1809 or later
- Play something in Spotify or a browser — it appears within a few seconds
- Switching videos in a browser may take up to 2 seconds to reflect — this is normal

**Wi-Fi / Bluetooth / audio not showing**
- If icons are stuck, right-click tray → Quit, then relaunch

**Bluetooth devices not appearing**
- Only paired devices are shown — pair via Windows Settings first
- Toggle Bluetooth off then on from the drawer if the list is stale

**Notifications not appearing**
- Open the drawer first — the notification panel only shows while the drawer is open
- Check that notification access is granted (see above)

**Virtual desktop dots not switching**
- Direct switching targets Windows 11 24H2 — on older builds it falls back to Win+Ctrl+Arrow

---

## Contributing

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for project structure, dev setup, build pipeline, and architecture notes.

---

## License

MIT
