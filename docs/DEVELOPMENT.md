# KrevonStation

Custom Windows taskbar/navbar - Ubuntu-style top bar built entirely from scratch using Win32 + Direct2D + DirectComposition. No frameworks, no external dependencies, pure C++ Win32.

---

## Developer Workflow

Use the root scripts for day-to-day work. You do not need Visual Studio to run the app during normal development.

```powershell
.\dev.ps1
```

Builds `Debug|x64`, creates/signs the local sparse package, ensures package identity is available for the Debug output folder, and starts the app. Use this whenever you need Windows notification listener or global media control support during development.

The first run may show a one-time UAC prompt to trust the local dev signing certificate for MSIX registration.

```powershell
.\dev.ps1 -NoLaunch
```

Builds and prepares package identity without launching. After this, Visual Studio `Debug | x64` -> `Start Without Debugging` can be used for the normal edit/build/run loop from the registered output folder.

The dev script intentionally does not unregister/re-register the package every run. Re-registering resets Windows capability/privacy permissions and can make Windows ask for notification/media/location/Wi-Fi style access again. If package capabilities or `Package.appxmanifest` change, force a one-time re-registration:

```powershell
.\dev.ps1 -ForceRegister
```

Build a release installer:

```powershell
.\release.ps1
```

Builds the production `Release|x64` app, creates the sparse MSIX, and builds the NSIS installer at `dist\Krevon Station Setup.exe`. This requires `KREVON_CERT_PFX` and `KREVON_CERT_PASSWORD` to point at a real signing certificate. For a local test installer only, run:

```powershell
.\release.ps1 -LocalTest
```

Version bumping is centralized:

```powershell
.\version.ps1 1.0.11
```

Publishing to GitHub Releases is also wrapped:

```powershell
.\publish.ps1 1.0.11
```

Use `.\publish.ps1 1.0.11 -Build` if you want it to build the installer first.

For full packaging notes, see [PACKAGING.md](PACKAGING.md).

---

## What It Is

A fully custom system navbar for Windows that replaces the default taskbar experience, complete with a high-fidelity Quick Settings slide-in drawer. Registered as a Windows AppBar so the OS permanently reserves its screen space - no application window can overlap it. GPU-accelerated, every pixel is manually rendered.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++20, MSVC x64 |
| Window System | Win32 API |
| Shell Integration | `SHAppBarMessage`, `Shell_NotifyIcon` |
| Image Loading | WIC (Windows Imaging Component) |
| 2D Rendering | Direct2D (`ID2D1DeviceContext5`) |
| SVG Icons | D2D native SVG - Lucide icon set |
| Audio | Core Audio (`IMMDeviceEnumerator`, `IAudioEndpointVolume`) |
| System Media | C++/WinRT `Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager` |
| Wi-Fi | Native WLAN API (`wlanapi`) with serialized background worker |
| Bluetooth | Classic desktop Bluetooth APIs |
| Notifications | C++/WinRT `UserNotificationListener` with sparse MSIX package identity |
| Animations | Adaptive `WM_TIMER` loops, time-based easing |
| UI Overlays | DirectComposition (`IDCompositionDevice`, `Target`, `Visual`) |
| GPU Interop | Direct3D 11 swap chain |
| COM Lifetime | `Microsoft::WRL::ComPtr<T>` |
| Blur/Backdrop | DWM (Desktop Window Manager) |

**No third-party libraries. No package managers. No frameworks.**

---

## Architecture

### Window
- `WS_POPUP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW` - no OS chrome, no taskbar entry, no Alt+Tab
- Registered as AppBar via `SHAppBarMessage` - Windows permanently reserves 32 DIP at top edge
- Hit testing done manually via `WM_NCHITTEST`
- DPI-aware: height scales as `32 x (DPI / 96)` physical pixels and recalculates on `WM_DPICHANGED`
- Receives status-provider refresh events via custom `WM_APP` messages and repaints through the normal render path

### AppBar Lifecycle
- `ABM_NEW` -> `ABM_QUERYPOS` -> `ABM_SETPOS`
- `ABN_POSCHANGED` - repositions when taskbar or another AppBar moves
- `ABN_FULLSCREENAPP` - drops to `HWND_BOTTOM` when a full-screen app opens, restores after
- `ABM_REMOVE` - always called before window destruction to release reserved space

### Renderer
- D3D11 device -> DXGI swap chain -> `ID2D1DeviceContext5`
- Device context DPI matches window DPI via `SetDpi`
- Per-primitive antialiasing enabled for crisp SVG edges
- `DXGI_SWAP_EFFECT_FLIP_DISCARD` for efficient presentation
- Renderer stays presentation-only: it receives a `StatusSnapshot` and chooses preloaded SVG variants at draw time

- The navbar center renders a compact Dynamic Island. With no active media it shows the clock, formatted like `Wed, May 6  10:58 PM`. When media is playing it shows the active media title with a small animated line visualizer. Clock/media text transitions crossfade so play/pause changes do not snap.
- Hovering the compact island opens a frameless DirectComposition popup centered below the 32 DIP AppBar. The popup morphs from compact size into the expanded island and closes smoothly when the cursor leaves both the compact island and popup. Repeated hover updates while already open are ignored so mouse movement does not rebuild media state every frame.
- Expanded fallback state shows the current time when Windows has no media sessions.
- Expanded media state shows cover art, app name, title, subtitle, playback controls, pagination, a playing-state light, and a playback progress slider. Cover art is center-cropped with rounded corners and falls back to a polished initial tile when metadata is incomplete.
- Playback sessions come from the official `Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager` API. The provider tracks session, metadata, playback, timeline, and current-session changes and publishes everything through the shared `StatusSnapshot`. The background worker combines event-driven GSMTC callbacks with a 2-second periodic poll so browser-originated video switches (which may delay `MediaPropertiesChanged` and `TimelinePropertiesChanged` by several seconds) are detected without waiting indefinitely for an event.
- Play/pause, previous, and next are routed through Windows media transport controls using `TryTogglePlayPauseAsync`, `TrySkipPreviousAsync`, and `TrySkipNextAsync`. Slider seeking uses `TryChangePlaybackPositionAsync` with Windows timeline properties. Disabled controls render dimmed and clicks no-op.
- The progress slider uses a simple rounded track, accent-filled progress bar, and thumb, and fades smoothly when switching between media sessions. Drag/click seeking suppresses stale timeline refreshes so the thumb does not jitter after a seek. On video switch, the slider suppresses display until the incoming timeline snapshot is confirmed to be newer than the detected track change (`mediaPropertiesChangedMs` field on `MediaSessionInfo`), preventing the old video's position from briefly advancing on the new track.
- Multiple media sessions use the same expanding/collapsing dot pager style as the virtual desktop indicator. Clicking a selected dot is ignored; hovering over the pager and using the mouse wheel switches between sessions.
- Session switching keeps pagination stable while app name crossfades, cover art scales/fades, title/subtitle swipe upward with fade, media buttons crossfade without moving, and slider state crossfades between sessions.
- Media API calls run on a background MTA worker so C++/WinRT async operations never block or assert on the navbar STA UI thread.
- Island memory is kept bounded by caching decoded cover art only while needed, comparing thumbnail keys by session/hash instead of storing duplicate byte copies, and clearing media snapshots/bitmaps when the expanded island closes.

### Quick Settings Drawer
- Independent, frameless `WS_POPUP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST` window spawned on clicking the status group.
- Uses `WS_EX_NOREDIRECTIONBITMAP` combined with **DirectComposition** to offload DWM presentation, eliminating alpha artifacts and flicker for silky-smooth overlays.
- Robust user avatar fetching via SID-based Registry lookups (`SOFTWARE\Microsoft\Windows\CurrentVersion\AccountPicture\Users\{SID}`) for Microsoft Accounts.
- Real-time interactive elements: Clickable split-pills for Wi-Fi and Bluetooth that directly toggle hardware radio states, aligned action buttons (camera, settings, lock, power), and a draggable volume slider that instantly syncs with Core Audio.
- Dynamic visual overrides: The volume UI cleanly intercepts the `volumeMuted` state to report 0% visually without corrupting the underlying volume level restored upon unmute.
- High-fidelity UI: Features layered separators, perfectly centered SVG icons dynamically colored at runtime, exact typographic alignments, and proportional spacing matching modern fluid design systems.
- **Smooth per-element hover animations**: Every interactive element (avatar, action buttons, Wi-Fi/Bluetooth pills, mute/unmute icon) has its own `[0, 1]` progress float driven by a dedicated `HOVER_TIMER`. Colors are interpolated each frame with `LerpColor()` so every hover-in and hover-out is a continuous ease rather than an instant switch. A light grey ring is drawn around the avatar and volume icon on hover.
- **Unified slide-in animation**: Opening and closing the drawer leverages a single D2D transparency layer combined with a global Y-axis translation. This ensures all drawer contents (avatar, icons, text, and sliders) smoothly slide down from the top and fade in perfectly in sync, with exact reversal on close.
- **Consistent height morphing**: Drawer sub-panel switches and dynamic list growth share one height-morph policy. Panel transitions compute duration from the current visible height delta using `HeightMorphDurationMs()`, while Sound, Wi-Fi, Bluetooth, and notification list rows feed their container height through `HeightMorphExtent()` so newly discovered devices, networks, sessions, and notifications expand with the same eased feel instead of dragging behind panel switches.
- **Button-triggered drawer close**: Clicking any top-row element (avatar, camera, settings, lock, power) closes the drawer with its animation. The screenshot button delays `VK_SNAPSHOT` until the drawer has fully hidden (`ANIM_OUT_MS + 30 ms`) so the drawer is absent from the Snipping Tool capture overlay. The avatar click opens Settings → Your Info after closing.
- **Click-outside to dismiss**: A global low-level mouse hook (`WH_MOUSE_LL`) automatically closes the drawer if the user clicks anywhere outside its bounds. Clicks on the navbar itself are intelligently routed to handle standard toggling behavior. The mouse hook is the sole authority for outside-click dismissal — no redundant handler exists inside the drawer's own `WM_LBUTTONDOWN` to prevent double-toggle races.
- **Navbar pill synchronisation**: When the drawer closes via a button or outside click, it posts `WM_APP_DRAWER_CLOSED` to the navbar HWND so `Renderer_SetDrawerOpen(false)` is called and the status-cluster pill fades out correctly.
- **Dynamic System Accent Color**: The drawer dynamically fetches the active Windows OS Accent Color using C++/WinRT (`Windows::UI::ViewManagement::UISettings`) and generates a cohesive palette of tonal variants (a light pastel variant for sliders and icons, and deep, low-opacity tinted variants for pill backgrounds) to perfectly match the user's desktop theme while maintaining readability. It also subscribes to `ColorValuesChanged` events to instantly regenerate this palette and update UI elements live if the user changes their Windows color.
- **Modular Panel System**: The drawer routes all sub-panels through a small internal contract: `DrawerPanel_GetHeight`, `DrawerPanel_Render`, `DrawerPanel_UpdateHover`, `DrawerPanel_HandleClick`, `DrawerPanel_HandleWheel`, and `DrawerPanel_Reset`. `drawer_wndproc.cpp` stays thin and delegates timers/input/render work to focused modules, while Power and Sound live in separate panel files. Transitioning between panels is driven by `PANEL_TIMER` (ID 13), `g_panelAnimProgress`, `EaseOutCubic`, and a height-delta duration clamp so small and large panel morphs feel consistent. The panel always resets to `Main` when the drawer opens. Adding Wi-Fi or Bluetooth later means adding a new `DrawerPanel` enum value, a panel module, registration in the panel dispatch, and Main-panel navigation wiring from the pill chevron zone.
- **Power Sub-Panel**: Clicking the Power button morphs the drawer into a Power panel containing Sleep (`moon.svg`), Restart (`rotate-ccw.svg`), and Shut Down (`power-off.svg`) options. Sleep invokes `SetSuspendState` via `PowrProf`. Restart and Shutdown use `ExitWindowsEx` with SE_SHUTDOWN_NAME privilege elevation via `Advapi32`. A `chevron-left.svg` back button in the panel header returns to the Main panel.
- **Sound Sub-Panel**: Clicking the settings button next to the volume slider morphs the drawer into a Sound panel. This panel lists all active Output Devices using `IMMDeviceEnumerator` and allows instant real-time endpoint switching via `IPolicyConfig` (IID `F8679F50-850A-41CF-9C72-430F290290C8`, CLSID `870AF99C-171D-4F9E-AF0D-E63DF40C2BC9`, verified against EarTrumpet — covers Windows 7 through current Windows 11). A Vista-era fallback CLSID (`294935CE`) is attempted if the primary fails. Clicking a device row is fully gated on a successful HRESULT from `SetDefaultEndpoint`; the checkmark moves optimistically on the same frame and a background `RefreshAll` confirms the real OS state. Section headings ("OUTPUT DEVICES", "VOLUME MIXER") use a dedicated `g_tfSndHdg` text format (9.5 pt Segoe UI Medium) for a bolder look distinct from body text. A 4 px gap separates each output device row. Below is a Volume Mixer that uses `IAudioSessionManager2` to enumerate all active audio sessions. It dynamically extracts the executable icon (`.exe`) via `ExtractIconExW` and WIC, caching them to D2D bitmaps. Each app gets its own volume slider with the percentage displayed inline on the right; tapping the icon instant-mutes that session and collapses the slider and percentage display to 0 (without corrupting the stored volume level so it restores on unmute). **Animated item entry/exit**: Devices and app sessions are tracked in parallel `g_animEndpoints` / `g_animSessions` vectors (separate from the raw `g_snap` data). When the OS snapshot changes, `SyncSoundPanelLists()` diffs the two sets — newly added items start at `alpha = 0, scale = 0.9` and animate in; removed items are marked `isRemoving = true` and fade/scale out before being erased. `SND_LIST_TIMER` (ID 14, 16 ms, shared `HEIGHT_MORPH_LIST_MS` timing) drives these per-item transitions. **Independent per-list scrolling**: The Output Devices list scrolls independently of the Volume Mixer. Devices cap their viewport at 3 visible rows; apps cap at 4. Each list has its own `PushAxisAlignedClip` region, its own scroll position (`g_sndDeviceScrollY` / `g_sndAppScrollY`), and its own scroll target (`g_sndDeviceScrollTargetY` / `g_sndAppScrollTargetY`). `WM_MOUSEWHEEL` routes to the correct list by comparing the cursor Y position against `g_sndMixerY`. Hit-testing for clicks (device selection, mute toggle, slider drag) is additionally gated against each list's clip rect so items scrolled out of view cannot be activated. **Spring-damper smooth scrolling**: `SCROLL_TIMER` (ID 15, 16 ms) runs a spring + friction integrator each frame — velocity is pulled toward the target with `stiffness = 0.18` and decayed by `damping = 0.78` — producing a momentum-style deceleration tail that settles cleanly below a 0.15 px threshold. The panel container height smoothly morphs frame-by-frame as items animate in/out, because list height sums eased `HeightMorphExtent()` row extents rather than counting raw snapshot items or linear alpha.
- **Wi-Fi Sub-Panel**: Clicking the right side of the Wi-Fi pill in the main navbar opens the Wi-Fi panel. This panel lists all available networks discovered via the Native Windows WLAN API.
  - **Dynamic List**: Networks are automatically sorted with the currently connected network pinned at the top. The list is scrollable using the same momentum-based spring-damper physics as the Sound panel.
  - **Connection State**: The panel visually distinguishes secured networks (lock icon `key.svg`) from open networks. Each `WifiNetwork` carries the saved WLAN profile name when one exists, so clicking a saved network can call `WlanConnect` directly without re-enumerating networks on the UI thread. Networks without saved profiles automatically close the drawer and route the user to the native Windows Settings App (`ms-settings:network-wifi`).
  - **Immediate Feedback**: Clicking a saved network instantly marks that row as connecting, clears stale checkmarks, and starts a rotating `refresh-cw.svg` spinner before Windows WLAN notifications arrive. The provider keeps the clicked SSID in `connectingSsid` with a 15 second timeout fallback so a failed or abandoned connection cannot spin forever.
  - **Radio Toggle Feedback**: Wi-Fi radio state is tracked separately from connection state through `StatusSnapshot::wifiRadioOn`, allowing the UI to distinguish Off, On but not connected, and Connected. Toggle clicks use `g_wifiTogglePending` plus `g_wifiToggleTargetOn` for optimistic feedback, showing "Turning on..." / "Turning off..." and a spinner until the background snapshot confirms the target state.
  - **Refresh Button**: A compact "Refresh" button at the bottom triggers an on-demand WLAN scan, featuring click-scale and spin animation. Scan/toggle/connect actions schedule a short background refresh burst so delayed networks, including slower-to-appear 5 GHz SSIDs, can populate after the first scan result.
  - **Empty State & Compaction**: If the Wi-Fi radio is disabled via the pill toggle, the panel dynamically shrinks its container height, removing the network list and centering a "Wi-Fi is turned off" message. If the radio is on but no networks are available yet, the panel shows "Looking for networks..." instead of presenting the state as off.
- **Dynamic `SetWindowRgn` click-through**: Every `Render()` call computes the drawer's current animated pixel height from the active panel morph plus any eased dynamic list or notification height, then applies it as a rectangular `HRGN` via `SetWindowRgn`. This masks the window shape to exactly its rendered content so clicks below the visible panel fall through to the desktop — regardless of which panel is active, mid-transition, or resizing from newly discovered content.
- **Drawer lifecycle safety**: When the drawer closes (any path — mouse hook, navbar button, power action), the `PANEL_TIMER` is killed immediately and `g_panelAnimDurMs` is zeroed so no in-progress panel transition can fire during the close animation and visually snap content to a different panel. `SetCapture` is only acquired for actual slider drag operations, preventing stale capture from re-routing outside clicks back through the drawer's message handler.

- **Bluetooth Sub-Panel**: Clicking the right side of the Bluetooth pill opens a dedicated paired-device panel. It mirrors the Wi-Fi panel shell with a back button, radio toggle, scrollable animated list, compact refresh button, and dynamic height compaction when Bluetooth is off.
  - **Paired Device List**: Devices come from classic desktop Bluetooth enumeration and are stored in `StatusSnapshot::bluetoothDevices`. Connected devices are pinned above paired idle devices, while all rows animate through `g_animBtDevices` using the shared list animation timer and spring-damper scrolling.
  - **Connect / Disconnect Rows**: Clicking a paired row attempts to connect it; clicking a connected row attempts to disconnect it. The word "Disconnect" is a static visual indicator on the connected row, not a separate button, so it has no hover chip or independent hover color.
  - **Busy-State Guard**: Bluetooth device actions are serialized by `status_monitor`. Once a connect or disconnect is accepted, all Bluetooth device-row clicks are ignored until the operation reaches its target state or times out after 15 seconds, preventing repeated connect/disconnect loops.
  - **Immediate Feedback**: Accepted actions mark the row as `isConnecting` or `isDisconnecting`, show "Connecting..." / "Disconnecting..." subtext, and spin `refresh-cw.svg` on the right while Windows updates the real device state. Disconnect does not optimistically clear `isConnected`; the row settles only after a refreshed snapshot confirms the OS state.
  - **Radio Toggle & Refresh**: The panel toggle uses `g_btTogglePending` plus `g_btToggleTargetOn` for "Turning Bluetooth on/off..." feedback, with a spinner in the switch. The refresh button runs the non-Wi-Fi status refresh path and uses the same click-scale/spin affordance as Wi-Fi.

- **Notification Panel**: When Windows toast notifications are available, a second rounded panel appears below the Quick Settings drawer. It uses the documented `UserNotificationListener` API, so the app must run with package identity through the sparse MSIX dev/release flow. Rows show app icon, app name, arrival time, title, and body text. The panel scrolls after three visible notifications, uses a slim custom scrollbar, and keeps the row close `X` hidden until the row is hovered. Notification rows use the same shared list timer and eased height extent as the drawer lists, so new and removed toasts expand/collapse at the same perceived speed as Wi-Fi, Bluetooth, and Sound panel changes. Clicking a row attempts best-effort source app activation by AppUserModelId; exact third-party toast action invocation is not exposed by the documented listener API.

### Status Icons
- Lucide SVGs are loaded at runtime from `assets/icons/` via `SHCreateStreamOnFileW` -> `CreateSvgDocument`
- SVG root `width`, `height`, and viewport are normalized after load so renderer constants control final icon size
- `stroke="currentColor"` and `color` are overridden dynamically (e.g. pinkish-red for pill icons, white for standard icons).
- Icons are sized accurately for DPI-consistent rendering
- The cluster is grouped on the right with a fully rounded hover capsule behind it

### Status Backend
- Wi-Fi state is fetched from the Native WLAN API using `WlanOpenHandle`, `WlanEnumInterfaces`, `WlanQueryInterface`, `WlanGetAvailableNetworkList`, `WlanScan`, `WlanSetInterface`, and `WlanConnect`.
- Wi-Fi operations that can stall (`Refresh`, `Toggle`, `Scan`, and `Connect`) are serialized through a dedicated `status_monitor` background worker. The navbar UI thread only queues work and receives `WM_APP_STATUS_CHANGED` snapshots, so Wi-Fi toggles, scans, and network switching do not block drawer animations or input.
- Wi-Fi notifications post `WM_APP_STATUS_PROVIDER_EVENT` with `STATUS_PROVIDER_EVENT_WIFI`; `window.cpp` routes those events to the async Wi-Fi queue instead of running full WLAN enumeration in the message pump. Audio/default device events still use the non-Wi-Fi refresh path.
- Scan, toggle, and connect actions start a 1 second interval follow-up refresh burst for roughly 8 seconds. This preserves delayed network discovery behavior without recursively refreshing on the UI thread.
- Bluetooth state is fetched from classic desktop Bluetooth APIs using `BluetoothFindFirstRadio`, `BluetoothFindFirstDevice`, `BluetoothGetDeviceInfo`, `BluetoothEnumerateInstalledServices`, and `BluetoothSetServiceState`. Hardware toggling is achieved via C++/WinRT `Windows::Devices::Radios` API.
- Bluetooth connect/disconnect actions are guarded by provider-owned pending state (`pendingDeviceAddressString`, target connected state, and start tick). The monitor accepts one device operation at a time, publishes an immediate busy snapshot, then posts follow-up non-Wi-Fi refresh events for roughly 8 seconds so delayed Windows Bluetooth state changes settle without recursive UI-thread work.
- Volume state is fetched from Core Audio using `IMMDeviceEnumerator` and `IAudioEndpointVolume`
- Media sessions are fetched from `GlobalSystemMediaTransportControlsSessionManager`. `MediaSessionInfo` includes source AUMID, readable app name, title, subtitle, playback state, enabled transport controls, timeline start/end/position, `mediaPropertiesChangedMs` (tick count when track identity last changed, used for stale timeline detection), current/selected session state, and thumbnail bytes for the expanded Dynamic Island. `FriendlyAppName` resolves the AUMID to a display name: packaged apps use `AppInfo::GetFromAppUserModelId`; non-packaged apps are matched against a known-app table (covering browsers, Spotify, VLC, YouTube Music Desktop App, and others) with fallbacks for path-based AUMIDs (`.exe` stripping), reverse-DNS AUMIDs (last component), and GUID-style AUMIDs (shown as "Media").
- Media provider events post `WM_APP_STATUS_PROVIDER_EVENT` with `STATUS_PROVIDER_EVENT_MEDIA` or `STATUS_PROVIDER_EVENT_MEDIA_READY`. `STATUS_PROVIDER_EVENT_MEDIA_READY` lets the MTA worker publish completed async media snapshots back through the normal navbar repaint path.
- Status logic is split into dedicated modules under `src/status/`
- `status_monitor` owns the providers, stores the latest `StatusSnapshot`, and posts `WM_APP_STATUS_CHANGED` only when state actually changes. `StatusSnapshot` includes `wifiRadioOn` so renderers can separate radio availability from connection/signal strength.

### Status Cluster Hover Pill
- The status icon group (Wi-Fi, Bluetooth, Volume) has a rounded pill background that is **only visible on hover or while the drawer is open**.
- Visibility is driven by `s_hoverProgress` (a `[0, 1]` float animated by `Renderer_TickAnimation`) which targets `1.0` when `s_hovered || s_drawerOpen` is true, and `0.0` only when both are false.
- The drawer's open state is tracked by `Renderer_SetDrawerOpen(bool)`. It is set to `true` when the user clicks the status cluster to open the drawer, and to `false` when the navbar receives `WM_APP_DRAWER_CLOSED` (posted by the drawer) or when the user clicks the cluster again to close it.
- `WM_MOUSELEAVE` no longer immediately fades the pill if the drawer is still open — the `wantLit` check in `Renderer_SetHover` silently absorbs the fade-out request.

### Mouse Interaction (Navbar)
- `WM_MOUSEMOVE` + `TrackMouseEvent` → hit-test against status cluster bounds, update hover target, arm timer
- `WM_MOUSELEAVE` → clears hover target, arms timer (pill fades only if drawer is also closed)
- `WM_TIMER` drives the hover animation loop. Active morphs/text transitions use a 16 ms interval, while compact-only media visualizer playback can throttle to a lighter interval.
- Animation is time-based: `elapsed / duration`
- Easing: `EaseOutCubic`
- Hit test converts physical pixel coordinates to DIPs using stored `dpiScale`
- Mouse wheel over the virtual desktop pager switches desktops, matching the Dynamic Island media pager wheel behavior.

### Hover Animation System (Drawer)
- Every interactive drawer element owns a dedicated `float` progress value in `[0, 1]`: `g_btnHovT[4]`, `g_pillHovT[2]`, `g_avHovT`, `g_volIconHovT`.
- A single `HOVER_TIMER` (ID 11, 16 ms) advances all values each tick by `16 / HOV_DUR_MS` toward their targets (1 = hovered, 0 = not) and auto-kills when all values have settled.
- `LerpColor(a, b, t)` is used by the drawer panel render modules instead of a binary branch, ensuring continuous color transitions for button circles, pill backgrounds (both ON and OFF states), and ring overlays.
- `SCREENSHOT_TIMER` (ID 12) is a one-shot timer set to `ANIM_OUT_MS + 30 ms` that fires `SendInput(VK_SNAPSHOT)` once the drawer window is fully hidden.

### System Tray
- Icon loaded from `assets/logo_transparent.png` via WIC and scaled to `SM_CXSMICON`
- Right-click -> **Check for updates...** or **Quit**
- Update checks read the latest public GitHub Release from `Krevon-Studios/Krevon-Station`. If a newer tag is available, the app downloads the installer asset, launches it, and exits so the installer can replace the current build.

---

## Project Structure

```text
KrevonStation/
|-- src/
|   |-- main.cpp                  - WinMain, COM init, DPI setup, timeBeginPeriod, message loop
|   |-- window.cpp                - Navbar HWND creation, WndProc, hover timer, AppBar callbacks, status messages
|   |-- shell.cpp                 - AppBar registration, system tray, PNG icon loader (WIC)
|   |-- renderer.cpp              - Navbar D3D11/D2D rendering, SVG status icons, hover capsule animation
|   |-- media_island.cpp          - DirectComposition Dynamic Island popup, media controls, pager, and animations
|   |-- drawer/                   - Quick Settings drawer implementation
|   |   |-- core/                 - Drawer lifecycle, initialization, and WndProc dispatch
|   |   |   |-- drawer.cpp
|   |   |   |-- drawer_init.cpp
|   |   |   `-- drawer_wndproc.cpp/.h
|   |   |-- interaction/          - Timers, animations, hit-testing, click routing, slider drags
|   |   |   |-- drawer_animation.cpp/.h
|   |   |   `-- drawer_input.cpp/.h
|   |   |-- render/               - Top-level render orchestration and shared render helpers
|   |   |   |-- drawer_render.cpp/.h
|   |   |   |-- drawer_app_icons.cpp
|   |   |   `-- drawer_notifications.cpp
|   |   |-- state/                - Shared drawer state, layout constants, enums, colors, helpers
|   |   |   |-- drawer_state.cpp/.h
|   |   |   `-- drawer_layout.h
|   |   |-- model/                - Drawer-side animated view models
|   |   |   `-- drawer_sound_model.cpp/.h
|   |   `-- panels/               - Drawer panel contract and panel implementations
|   |       |-- drawer_panel.h
|   |       |-- drawer_panel_main.cpp
|   |       |-- drawer_panel_power.cpp
|   |       |-- drawer_panel_sound.cpp
|   |       |-- drawer_panel_wifi.cpp
|   |       `-- drawer_panel_bluetooth.cpp
|   `-- status/
|       |-- wifi_status.cpp       - WLAN-backed Wi-Fi radio, scan, profile, and connection state
|       |-- bluetooth_status.cpp  - Bluetooth radio, paired device, and connect/disconnect state
|       |-- audio_status.cpp      - Core Audio volume, sessions, endpoint switching
|       |-- media_status.cpp      - Windows media sessions, metadata, transport controls, thumbnails
|       |-- notification_status.cpp - Windows notification listener, parsing, remove/clear/activate
|       `-- status_monitor.cpp    - Status coordinator, async Wi-Fi/media workers, snapshot publisher
|-- include/
|   |-- window.h                  - NAVBAR_HEIGHT_DIP, window API
|   |-- shell.h                   - AppBar + tray API, WM_ message IDs
|   |-- renderer.h                - Navbar renderer API, hit-test, hover animation, status snapshot sink
|   |-- media_island.h            - Dynamic Island popup lifecycle and snapshot sink
|   |-- drawer.h                  - Public Drawer API
|   `-- status/
|       |-- status_types.h        - Shared icon enums, WifiNetwork, and StatusSnapshot
|       |-- wifi_status.h         - Wi-Fi provider API
|       |-- bluetooth_status.h    - Bluetooth provider API
|       |-- audio_status.h        - Audio provider API
|       |-- media_status.h        - Media provider API
|       |-- notification_status.h - Notification listener API
|       `-- status_monitor.h      - Status coordinator API
`-- assets/
    |-- icons/                  - Lucide SVG files for status and drawer panel icons
    |-- logo_solid.png
    `-- logo_transparent.png
```

Future drawer panels should follow the existing panel contract under `src/drawer/panels/`: add a `DrawerPanel` enum value in `src/drawer/state/drawer_layout.h`, create a `src/drawer/panels/drawer_panel_<name>.cpp` module, register height/render/input behavior in the panel dispatch functions, and wire Main-panel navigation from the appropriate chevron/right-side pill region.

---

## Message IDs
```text
drawer.h      WM_APP_DRAWER_CLOSED  (WM_APP + 3)  drawer → navbar: pill fade-out
drawer.h      WM_APP_NOTIFICATIONS_CHANGED (WM_APP + 4)  notification listener -> navbar/drawer refresh
shell.h       WM_APPBAR_CALLBACK    (WM_USER + 1)  AppBar position/state callbacks
status_monitor.h
              WM_APP_STATUS_PROVIDER_EVENT (WM_APP + 1)  raw provider thread wakeup; wParam can be STATUS_PROVIDER_EVENT_WIFI, STATUS_PROVIDER_EVENT_MEDIA, or STATUS_PROVIDER_EVENT_MEDIA_READY
              WM_APP_STATUS_CHANGED        (WM_APP + 2)  snapshot changed, repaint
```

---

## Build Requirements

- **Visual Studio 2022** with "Desktop development with C++" workload
- **Windows SDK 10.0.26100.0**
- **MSVC** toolset v145, x64 only
- **C++20** (`/std:c++20`)
- **NSIS** for installer builds through `release.ps1`
- **Package identity** for notification listener and global media control support. Use `dev.ps1` locally or the NSIS installer in release builds.
- **Manifest capabilities**: `uap3:userNotificationListener` for toast notifications and `uap7:globalMediaControl` for Windows media sessions.

### Linked Libraries

```text
d2d1.lib
dwrite.lib
dcomp.lib
d3d11.lib
dxgi.lib
shcore.lib
dwmapi.lib
shell32.lib
shlwapi.lib
WindowsCodecs.lib
wlanapi.lib
Bthprops.lib
winmm.lib
windowsapp.lib
PowrProf.lib
Advapi32.lib
```

---

## Building

**Recommended CLI dev loop:**

```powershell
.\dev.ps1
```

**Build/register without launching:**

```powershell
.\dev.ps1 -NoLaunch
```

**Visual Studio:**
1. Open `KrevonStation.slnx`
2. Select `Debug | x64` or `Release | x64`
3. `Ctrl+Shift+B` to build, `F5` to run

For notification listener or media-control testing from Visual Studio, run `.\dev.ps1 -NoLaunch` at least once first so the Debug output folder has package identity.

**MSBuild (CLI):**
```powershell
msbuild KrevonStation.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Output: `x64\Debug\Krevon Station.exe`

> Post-build step automatically copies `assets\` next to the exe.

**Release installer:**

```powershell
.\release.ps1
```

For local installer testing with a dev certificate:

```powershell
.\release.ps1 -LocalTest
```

---

## Status Icon Mapping

### Wi-Fi
- `wifi.svg` - strong connected signal
- `wifi-high.svg` - high connected signal
- `wifi-low.svg` - low connected signal
- `wifi-zero.svg` - very weak connected signal
- `globe-off.svg` - Wi-Fi radio off or disconnected / no connection

### Bluetooth
- `bluetooth-connected.svg` - a Bluetooth device is connected
- `bluetooth.svg` - radio is available but no device is actively connected
- `bluetooth-off.svg` - radio unavailable or off

### Volume
- `volume.svg` - low volume
- `volume-1.svg` - medium volume
- `volume-2.svg` - high volume
- `volume-off.svg` - volume at `0%`
- `volume-x.svg` - muted

---

## Adding Icons

Drop any [Lucide](https://lucide.dev) SVG into `assets/icons/` and rebuild. Status SVGs are loaded by filename during renderer initialization, so the runtime mapping expects these filenames for the built-in status states:

| File | Meaning |
|---|---|
| `bluetooth-connected.svg` | Bluetooth device connected |
| `bluetooth.svg` | Bluetooth radio on |
| `bluetooth-off.svg` | Bluetooth unavailable/off |
| `wifi.svg` | Strong Wi-Fi signal |
| `wifi-high.svg` | High Wi-Fi signal |
| `wifi-low.svg` | Low Wi-Fi signal |
| `wifi-zero.svg` | Very weak Wi-Fi signal |
| `globe-off.svg` | Wi-Fi radio off or disconnected / no connection |
| `volume.svg` | Low volume |
| `volume-1.svg` | Medium volume |
| `volume-2.svg` | High volume |
| `volume-off.svg` | Volume at 0% |
| `volume-x.svg` | Muted |
| `speaker.svg` | Sound panel — System sounds / default app icon |
| `headphones.svg` | Sound panel — Audio endpoint device |
| `moon.svg` | Power panel — Sleep |
| `rotate-ccw.svg` | Power panel — Restart |
| `power-off.svg` | Power panel — Shut Down |
| `chevron-left.svg` | Panel header — Back button |
| `check.svg` | Panel list — Active selection |
| `refresh-cw.svg` | Wi-Fi/Bluetooth panels — Scan/connect/disconnect spinner |
| `key.svg` | Wi-Fi panel — Secured network |
| `play.svg` | Dynamic Island media controls - Play |
| `pause.svg` | Dynamic Island media controls - Pause |
| `rewind.svg` | Dynamic Island media controls - Previous |
| `fast-forward.svg` | Dynamic Island media controls - Next |

---

## Coding Rules

- `ComPtr<T>` for every COM object - never raw COM pointers
- DPI aware: `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` at startup, handle `WM_DPICHANGED`
- No heap allocations in the render loop
- Each subsystem initializes in its own function or module
- Animation values are time-based, not frame-delta-based
- Render directly from `WM_TIMER` during animation and suppress redundant paint work with `ValidateRect`

---

## Platform

- Windows 10 (1809+) / Windows 11 (x64) - D2D SVG requires Creators Update minimum; Dynamic Island media controls require `GlobalSystemMediaTransportControlsSessionManager`
- Recommended SDK: 10.0.26100.0
