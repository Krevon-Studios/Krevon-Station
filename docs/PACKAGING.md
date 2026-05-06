# Packaging And Releases

Krevon Station's native rewrite keeps the production identity from the Electron app:

- `appId`: `com.krevon.station`
- `productName`: `Krevon Station`
- installer: per-user NSIS
- install folder: `%LOCALAPPDATA%\Programs\Krevon Station`
- startup registry value: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\KrevonStation`

Keeping those values lets the native installer replace the old Electron install instead of creating a separate app entry.

## Daily Development

Use:

```powershell
.\dev.ps1
```

This command:

- stops a running dev copy so the linker can overwrite the exe
- builds `Debug|x64`
- creates/signs `artifacts\KrevonStation-dev.msix`
- ensures sparse package identity is registered for `x64\Debug`
- launches `x64\Debug\Krevon Station.exe`

The package identity step is required for Windows notification listener support and Windows global media control access.

The first run may show a one-time UAC prompt to trust the local dev signing certificate for MSIX registration. After that, normal dev runs should not show the Windows capability/privacy prompt again.

## Visual Studio

If you want Visual Studio to launch the app:

```powershell
.\dev.ps1 -NoLaunch
```

Then use Visual Studio with `Debug | x64` and `Start Without Debugging`.

Plain Visual Studio launch can build and run the app, but notification listener and global media control access depend on package identity being registered first.

## Package Re-Registration

Normal `.\dev.ps1` does not unregister/re-register the sparse package every run. Re-registering resets Windows capability permissions and causes repeated prompts.

Only force re-registration when `Package.appxmanifest`, package capabilities, package identity, or the registered output location changes:

```powershell
.\dev.ps1 -ForceRegister
```

This is required after adding or changing capabilities such as `uap3:userNotificationListener` or `uap7:globalMediaControl`, because Windows only re-evaluates the sparse package manifest during registration.

## Version Bumps

Use:

```powershell
.\version.ps1 1.0.11
```

This updates:

- `VERSION`
- `Package.appxmanifest`
- `app.rc`
- `installer\KrevonStation.nsi`

The public app version is three-part, such as `1.0.11`. The Windows package/resource version is four-part, such as `1.0.11.0`.

## Local Test Installer

Install NSIS, then run:

```powershell
.\release.ps1 -LocalTest
```

Output:

```text
dist\Krevon Station Setup.exe
```

`-LocalTest` uses the local dev certificate. That is only for your machine and test installs.

## Production Installer

For production, use a real code-signing certificate:

```powershell
$env:KREVON_CERT_PFX="D:\secure\KrevonStation.pfx"
$env:KREVON_CERT_PASSWORD="..."
.\release.ps1
```

The release command builds `Release|x64`, creates `artifacts\KrevonStation.msix`, signs it, and produces:

```text
dist\Krevon Station Setup.exe
```

The installer copies:

- `x64\Release\Krevon Station.exe`
- `x64\Release\assets\`
- `artifacts\KrevonStation.msix`

Then it registers sparse package identity with:

```powershell
Add-AppxPackage -Path "$INSTDIR\KrevonStation.msix" -ExternalLocation "$INSTDIR"
```

That identity is required for `UserNotificationListener` and `GlobalSystemMediaTransportControlsSessionManager`.

## GitHub Releases

Publishing uses GitHub CLI. First authenticate once:

```powershell
gh auth login
```

Upload an existing installer:

```powershell
.\publish.ps1 1.0.11
```

Build and publish in one command:

```powershell
.\publish.ps1 1.0.11 -Build
```

If release `v1.0.11` already exists, the asset is uploaded with `--clobber`. Otherwise the script creates the release and uploads `dist\Krevon Station Setup.exe`.

## App Updates

The app checks the latest public GitHub Release from `Krevon-Studios/Krevon-Station` on startup and exposes the same check from the tray menu:

```text
Right-click tray icon -> Check for updates...
```

If the latest release tag is newer than the installed app version, the updater looks for a Windows `.exe` release asset whose URL contains `Krevon` and `Setup`, downloads it to the user's temp folder, launches it, and asks the running app to close. The installer then performs the normal in-place replacement flow.

Release tags should stay in the normal `v1.2.0` format. The installer asset is versioned:

```text
Krevon Station Setup 1.2.0.exe
```

Startup checks are quiet when GitHub is unreachable or no update is available. Manual tray checks show either an up-to-date message or an error.

## Notification Listener Notes

The notification panel uses `Windows.UI.Notifications.Management.UserNotificationListener`.

Important constraints:

- It only works when the app runs with package identity.
- The package manifest must include `uap3:Capability Name="userNotificationListener"`.
- Windows permission is user-controlled and can be changed in system privacy settings.
- The listener can read/remove/clear current toast notifications, but it does not expose the original third-party toast action. Row click uses best-effort source app activation through AppUserModelId.
- Use `.\dev.ps1 -NoLaunch` before Visual Studio or manual Debug runs when validating notification-panel rendering or height animation. That keeps the Debug output registered with package identity without launching a second app instance.

## Global Media Control Notes

The Dynamic Island media player uses `Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager`.

Important constraints:

- It only works when the app runs with package identity.
- The package manifest must include `uap7:Capability Name="globalMediaControl"`.
- After adding or changing this capability, run `.\dev.ps1 -ForceRegister -NoLaunch` once so Windows picks up the updated manifest.
- Windows 10 1809 or newer is required for the global media control API.
- Media commands are routed through the OS transport controls: play/pause uses `TryTogglePlayPauseAsync`, previous uses `TrySkipPreviousAsync`, and next uses `TrySkipNextAsync`.
- Playback slider seeking uses the session timeline from `GetTimelineProperties` and sends seek requests with `TryChangePlaybackPositionAsync`. Timeline values are in 100 ns ticks, so code should keep them as `int64_t`/`long long` rather than converting through floating point until render time.
- Session metadata, playback properties, timeline updates, and current-session changes can arrive independently. Keep those event handlers lightweight and let the status monitor publish coalesced snapshots instead of doing UI work in the provider callback.
- The media provider runs WinRT async work on a background MTA worker and publishes completed snapshots back to the navbar through the normal status-monitor path. This keeps the Direct2D UI thread responsive and avoids STA-thread WinRT assertions.
- The expanded island intentionally releases cached media snapshots, decoded cover bitmaps, and transition thumbnails when it closes. Avoid retaining raw thumbnail byte copies in UI state; use lightweight keys such as session id, byte size, and hash to decide when cover art needs to be decoded again.
- The compact island can remain visible for long playback sessions, so animation timers should be adaptive. Use the fast 16 ms interval only for active morphs/text transitions and allow compact-only visualizer playback to run at the lighter interval exposed by the renderer.

## Common Commands

```powershell
.\dev.ps1
.\dev.ps1 -NoLaunch
.\dev.ps1 -ForceRegister
.\version.ps1 1.0.11
.\release.ps1 -LocalTest
.\release.ps1
.\publish.ps1 1.0.11
.\publish.ps1 1.0.11 -Build
```
