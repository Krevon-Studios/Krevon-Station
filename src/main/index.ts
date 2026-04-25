import { app, ipcMain, screen, powerMonitor, systemPreferences } from 'electron'
import { createIslandWindow, createTaskbarWindow, createDrawerWindow, createNotificationWindow, TASKBAR_H } from './window'
import { startHookServer } from './hook-server'
import { startMediaWatcher, controlMedia } from './media-watcher'
import { startDesktopWatcher, switchVirtualDesktop } from './desktop-watcher'
import { createTray } from './tray'
import { attachAppBar } from './appbar'
import { startSystemStatsWatcher, getCachedSystemStats, setAudioVolume, setAudioMute, requestAudioSessions, setSessionVolume, requestAudioDevices, setAudioDevice, scriptPath } from './system-stats'
import { spawn, exec } from 'child_process'
import { createInterface } from 'readline'

app.setName('Dynamic Island')

if (!app.requestSingleInstanceLock()) {
  app.quit()
  process.exit(0)
}

// ─────────────────────────────────────────────────────────────────────────────

app.whenReady().then(() => {
  const taskbarWin = createTaskbarWindow()
  const islandWin = createIslandWindow()
  const drawerWin = createDrawerWindow()
  const notifWin  = createNotificationWindow()
  let appBarRect = { left: 0, top: 0, right: 0, bottom: 32 }
  let taskbarShown = false

  const detachAppBar = attachAppBar(
    taskbarWin,
    (rect) => {
      appBarRect = rect
      taskbarWin.setBounds({ x: rect.left, y: rect.top, width: rect.right - rect.left, height: TASKBAR_H })
      if (!taskbarShown && !taskbarWin.isDestroyed()) { taskbarShown = true; taskbarWin.show() }
      pinIslandToTop()
      syncDrawerPosition()
    },
    (isFullscreen) => {
      if (taskbarWin.isDestroyed() || islandWin.isDestroyed()) return
      if (isFullscreen) {
        taskbarWin.setOpacity(0); taskbarWin.setIgnoreMouseEvents(true, { forward: true })
        islandWin.setOpacity(0); islandWin.setIgnoreMouseEvents(true, { forward: true })
        if (!drawerWin.isDestroyed() && drawerOpen) {
          drawerOpen = false
          drawerWin.setIgnoreMouseEvents(true, { forward: true })
          drawerWin.webContents.send('drawer:force-close')
        }
        if (!notifWin.isDestroyed()) notifWin.hide()
      } else {
        taskbarWin.setOpacity(1); islandWin.setOpacity(1)
        islandWin.setAlwaysOnTop(true, 'pop-up-menu')
        interactActive = -1 as any; hoverActive = -1 as any; taskbarInteractActive = -1 as any
        reassertWindows('fullscreen-exit')
      }
    }
  )

  islandWin.once('show', () => pinIslandToTop())

  // ── Accent color ───────────────────────────────────────────────────────────

  const allWins = [taskbarWin, islandWin, drawerWin, notifWin]

  function parseAccent() {
    const raw = systemPreferences.getAccentColor()
    return { r: parseInt(raw.slice(0, 2), 16), g: parseInt(raw.slice(2, 4), 16), b: parseInt(raw.slice(4, 6), 16) }
  }

  function broadcastAccent() {
    const payload = parseAccent()
    for (const w of allWins) { if (!w.isDestroyed()) w.webContents.send('accent-color', payload) }
  }

  const onAccentChanged = () => broadcastAccent()
  systemPreferences.on('accent-color-changed', onAccentChanged)

  for (const w of allWins) { w.webContents.once('did-finish-load', () => broadcastAccent()) }

  ipcMain.handle('get-accent-color', () => parseAccent())

  startHookServer(islandWin)
  startMediaWatcher(islandWin)
  startDesktopWatcher(taskbarWin)
  startSystemStatsWatcher([taskbarWin, drawerWin])
  createTray([taskbarWin, islandWin])

  // Buffer notifications that arrive before notifWin renderer is ready
  let notifWinReady = false
  const notifBuffer: object[] = []

  notifWin.webContents.once('did-finish-load', () => {
    notifWinReady = true
    // Replay any notifications buffered before the renderer finished loading
    for (const evt of notifBuffer) {
      if (!notifWin.isDestroyed()) notifWin.webContents.send('island:notifications', evt)
    }
    notifBuffer.length = 0
  })

  function sendToNotifWin(data: object) {
    if (notifWin.isDestroyed()) return
    if (notifWinReady) {
      notifWin.webContents.send('island:notifications', data)
    } else {
      notifBuffer.push(data)
    }
  }

  startNotificationMonitor()

  // ── Position helpers ───────────────────────────────────────────────────────

  function pinIslandToTop(retriesLeft = 5) {
    if (islandWin.isDestroyed()) return
    const { bounds } = screen.getPrimaryDisplay()
    islandWin.setBounds({
      x: bounds.x + Math.floor((bounds.width - islandWin.getBounds().width) / 2),
      y: bounds.y,
      width: islandWin.getBounds().width,
      height: islandWin.getBounds().height,
    })
    if (islandWin.getBounds().y !== bounds.y && retriesLeft > 0) setTimeout(() => pinIslandToTop(retriesLeft - 1), 50)
  }

  function syncDrawerPosition() {
    if (drawerWin.isDestroyed()) return
    const { bounds } = screen.getPrimaryDisplay()
    const W = 340
    const H = 480
    drawerWin.setBounds({ x: bounds.x + bounds.width - W, y: bounds.y + TASKBAR_H, width: W, height: H })
  }

  function syncNotifPosition() {
    // Notif window spans full available height from TASKBAR_H to screen bottom.
    // The React panel positions itself inside via CSS `top`. This prevents bottom clipping
    // when the drawer grows and pushes the panel down.
    if (notifWin.isDestroyed()) return
    const { bounds } = screen.getPrimaryDisplay()
    const H = bounds.height - TASKBAR_H
    notifWin.setBounds({ x: bounds.x + bounds.width - 340, y: bounds.y + TASKBAR_H, width: 340, height: H })
  }

  function startNotificationMonitor(exe = 'py') {
    const proc = spawn(exe, [scriptPath('notification-monitor.py')], { windowsHide: true })

    createInterface({ input: proc.stdout }).on('line', (line) => {
      try {
        const data = JSON.parse(line.trim())
        sendToNotifWin(data)
      } catch { /* malformed line */ }
    })

    proc.stderr.on('data', (d: Buffer) => {
      const msg = d.toString().trim()
      if (msg) console.error('[notif:err]', msg)
    })

    proc.on('error', () => {
      if (exe === 'py') setTimeout(() => startNotificationMonitor('python'), 1000)
    })

    proc.on('close', () => {
      setTimeout(() => { if (!notifWin.isDestroyed()) startNotificationMonitor(exe) }, 3000)
    })
  }

  // ── IPC: System stats ──────────────────────────────────────────────────────

  ipcMain.handle('get-system-stats', () => getCachedSystemStats())

  // ── IPC: Drawer ────────────────────────────────────────────────────────────

  ipcMain.on('drawer:open', (_e, type: string) => {
    if (drawerWin.isDestroyed()) return
    drawerOpen = true
    syncDrawerPosition()
    drawerWin.webContents.send('drawer:show', type)
    drawerWin.show()
    drawerWin.focus()
    if (!notifWin.isDestroyed()) {
      syncNotifPosition()
      notifWin.webContents.send('drawer:show', type)          // wake up NotificationCards
      notifWin.webContents.send('drawer:height', drawerWin.getBounds().height)
      notifWin.show()
    }
  })

  drawerWin.on('blur', () => {
    if (drawerOpen && !drawerWin.isDestroyed()) {
      drawerWin.webContents.send('drawer:force-close')
      if (!notifWin.isDestroyed()) notifWin.webContents.send('drawer:force-close')
    }
  })

  // Sent by taskbar/external callers
  ipcMain.on('drawer:request-close', () => {
    if (!drawerWin.isDestroyed()) drawerWin.webContents.send('drawer:force-close')
    if (!notifWin.isDestroyed()) notifWin.webContents.send('drawer:force-close')
  })

  // Sent by React after its close animation finishes
  ipcMain.on('drawer:close', () => {
    drawerOpen = false
    taskbarWin.webContents.send('drawer:closed')
    if (!drawerWin.isDestroyed()) drawerWin.hide()
    if (!notifWin.isDestroyed()) notifWin.hide()
  })

  // Resize drawer window to match actual card height — eliminates transparent dead zone
  // Notification window is NOT touched here — it stays fixed at its permanent bounds.
  ipcMain.on('drawer:resize', (_e, h: number) => {
    if (drawerWin.isDestroyed()) return
    const { bounds } = screen.getPrimaryDisplay()
    const clamped = Math.max(Math.ceil(h), 60)
    drawerWin.setBounds({
      x: bounds.x + bounds.width - 340,
      y: bounds.y + TASKBAR_H,
      width: 340,
      height: clamped,
    })
    // Tell the notif overlay how tall the drawer card is so it can CSS-position below it
    if (!notifWin.isDestroyed() && notifWin.isVisible()) {
      notifWin.webContents.send('drawer:height', clamped)
    }
  })

  // notification:resize is no longer used — notif window stays at fixed 480px height.
  // Kept as a no-op so old IPC calls don't throw.
  ipcMain.on('notification:resize', () => { /* no-op — window is fixed size */ })

  // ── IPC: Volume — writes directly to Python process stdin ──────────────────

  ipcMain.handle('set-system-volume', (_e, volume: number) => {
    setAudioVolume(Math.max(0, Math.min(100, volume)))
  })

  ipcMain.handle('set-system-mute', (_e, muted: boolean) => {
    setAudioMute(muted)
  })

  ipcMain.handle('set-app-volume', (_e, _pid: number, _vol: number) => {
    // Best-effort — would need per-session ISimpleAudioVolume COM
  })

  // ── IPC: Audio devices ─────────────────────────────────────────────────────

  ipcMain.handle('get-audio-devices', () => requestAudioDevices())

  ipcMain.handle('get-audio-sessions', () => requestAudioSessions())

  ipcMain.handle('set-session-volume', (_e, pid: number, volume?: number, muted?: boolean) => {
    setSessionVolume(pid, volume, muted)
  })

  ipcMain.handle('set-audio-device', (_e, deviceId: string) => {
    setAudioDevice(deviceId)
  })

  // ── IPC: App icon by PID (base64 PNG) ─────────────────────────────────────
  ipcMain.handle('get-app-icon', async (_e, pid: number) => {
    try {
      const { exec } = await import('child_process')
      const exePath: string = await new Promise((resolve) => {
        exec(
          `wmic process where ProcessId=${pid} get ExecutablePath /format:value`,
          { windowsHide: true } as any,
          (_err: Error | null, stdout: string) => {
            const m = stdout.match(/ExecutablePath=(.+)/i)
            resolve(m ? m[1].trim() : '')
          }
        )
      })
      if (!exePath) return null
      const icon = await app.getFileIcon(exePath, { size: 'normal' })
      return icon.toDataURL()            // "data:image/png;base64,..."
    } catch {
      return null
    }
  })

  // ── IPC: Notification app icon by AUMID (UWP package icon) ────────────────
  const _notifIconCache = new Map<string, string | null>()

  ipcMain.handle('get-notif-icon', async (_e, appId: string) => {
    if (!appId) return null
    if (_notifIconCache.has(appId)) return _notifIconCache.get(appId) ?? null

    return new Promise<string | null>((resolve) => {
      // Extract Package Family Name from AUMID (Publisher.AppName_Hash!EntryPoint)
      const pfn = (appId.split('!')[0] ?? appId).replace(/'/g, "''")

      // Build PS script as a plain string — no JS interpolation inside the PS body
      const psLines = [
        `$pfn = '${pfn}'`,
        `$pkg = Get-AppxPackage | Where-Object { $_.PackageFamilyName -eq $pfn } | Select-Object -First 1`,
        `if (-not $pkg) { Write-Output ''; exit }`,
        `try {`,
        `  [xml]$m = Get-Content "$($pkg.InstallLocation)\\AppxManifest.xml" -EA Stop`,
        `  $logo = $m.Package.Applications.Application.VisualElements.Square44x44Logo`,
        `  if (-not $logo) { $logo = $m.Package.Properties.Logo }`,
        `  $base = Join-Path $pkg.InstallLocation $logo`,
        `  $found = $null`,
        // Use a PS foreach with $scale var — no JS template interpolation here
        `  foreach ($scale in @('scale-100','scale-150','scale-200','')) {`,
        `    $candidate = if ($scale) { $base -replace '\\.png$',("." + $scale + ".png") } else { $base }`,
        `    if (Test-Path $candidate) { $found = $candidate; break }`,
        `  }`,
        `  if ($found) { $b = [System.IO.File]::ReadAllBytes($found); Write-Output ('data:image/png;base64,' + [Convert]::ToBase64String($b)) }`,
        `  else { Write-Output '' }`,
        `} catch { Write-Output '' }`,
      ].join("\n")

      // Use -EncodedCommand to avoid all shell-quoting issues
      const encoded = Buffer.from(psLines, 'utf16le').toString('base64')

      exec(
        `powershell.exe -NoProfile -NonInteractive -EncodedCommand ${encoded}`,
        { windowsHide: true, timeout: 10000 } as any,
        (_err: Error | null, stdout: string) => {
          const result = stdout.trim() || null
          _notifIconCache.set(appId, result)
          resolve(result)
        }
      )
    })
  })

  ipcMain.handle('clear-notifications', async (_e, appIds: string[]) => {
    if (!appIds || appIds.length === 0) return
    const uniqueIds = [...new Set(appIds.filter(Boolean))]
    const psLines = [
      `Add-Type -AssemblyName System.Runtime.WindowsRuntime`,
      `$null = [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]`,
      `$history = [Windows.UI.Notifications.ToastNotificationManager]::History`,
      ...uniqueIds.map(id => `try { $history.Clear('${id.replace(/'/g, "''")}') } catch {}`)
    ].join("\n")
    const encoded = Buffer.from(psLines, 'utf16le').toString('base64')
    exec(`powershell.exe -NoProfile -NonInteractive -EncodedCommand ${encoded}`, { windowsHide: true })
  })

  // ── IPC: WiFi ─────────────────────────────────────────────────────────────

  ipcMain.handle('scan-wifi-networks', async (_e, force: boolean = true) => {
    // Run wifi-scan.py — uses Windows WLAN API (wlanapi.dll) directly via
    // ctypes, the same path Windows Settings takes. It triggers WlanScan,
    // waits for results, then returns a JSON array via stdout.
    return new Promise<{ ssid: string; signal: number; secured: boolean; connected: boolean }[]>((resolve) => {
      const script = scriptPath('wifi-scan.py')
      const args = force ? [script] : [script, '--no-scan']
      const tryExe = (exe: string) => {
        const proc = spawn(exe, args, { windowsHide: true })
        let stdout = ''
        proc.stdout.on('data', (d: Buffer) => (stdout += d.toString()))
        proc.stderr.on('data', (d: Buffer) => {
          const msg = d.toString().trim()
          if (msg) console.error(`[wifi-scan:err] ${msg}`)
        })
        proc.on('close', () => {
          try { resolve(JSON.parse(stdout.trim())) } catch { resolve([]) }
        })
        proc.on('error', () => {
          if (exe === 'py') tryExe('python')
          else resolve([])
        })
      }
      tryExe('py')
    })
  })

  ipcMain.handle('set-wifi-enabled', async (_e, enable: boolean) => {
    return new Promise<void>((resolve) => {
      const script = scriptPath('wifi-toggle.py')
      exec(`py "${script}" ${enable ? '--enable' : '--disable'}`, { windowsHide: true }, () => {
        resolve()
      })
    })
  })

  ipcMain.handle('get-wifi-state', async () => {
    return new Promise<{ enabled: boolean }>((resolve) => {
      exec('netsh wlan show interfaces', { windowsHide: true }, (_err, stdout) => {
        if (!stdout || stdout.includes('no wireless interface')) return resolve({ enabled: false })
        const isOff = stdout.includes('Software Off') || stdout.includes('Hardware Off')
        resolve({ enabled: !isOff })
      })
    })
  })

  ipcMain.handle('connect-wifi', async (_e, ssid: string) => {
    return new Promise<void>((resolve) => {
      exec(`netsh wlan connect name="${ssid}"`, { windowsHide: true }, () => resolve())
    })
  })

  // ── IPC: User info (avatar + display name) ────────────────────────────────

  ipcMain.handle('get-user-info', async () => {
    const fs = require('fs') as typeof import('fs')
    const path = require('path') as typeof import('path')
    const os = require('os') as typeof import('os')

    // Display name — FullName from local user, fallback to login name
    const name: string = await new Promise(resolve => {
      exec(
        'powershell.exe -NoProfile -NonInteractive -Command "try{(Get-LocalUser -Name $env:USERNAME).FullName}catch{$env:USERNAME}"',
        { windowsHide: true },
        (_err, stdout) => resolve((stdout.trim() || os.userInfo().username))
      )
    })

    const readImg = (p: string): string | null => {
      try {
        const data = fs.readFileSync(p)
        const isPng = data[0] === 0x89 && data[1] === 0x50
        const isJpeg = data[0] === 0xFF && data[1] === 0xD8
        const isBmp = data[0] === 0x42 && data[1] === 0x4D
        const isWebP = data[0] === 0x52 && data[1] === 0x49 && data[2] === 0x46 && data[3] === 0x46 &&
          data[8] === 0x57 && data[9] === 0x45 && data[10] === 0x42 && data[11] === 0x50
        if (!isPng && !isJpeg && !isBmp && !isWebP) return null
        const mime = isPng ? 'png' : isJpeg ? 'jpeg' : isBmp ? 'bmp' : 'webp'
        return `data:image/${mime};base64,${data.toString('base64')}`
      } catch { return null }
    }

    let avatar: string | null = null

    // Get user SID (needed for Win11 MS account picture paths)
    const sid: string = await new Promise(resolve => {
      exec(
        `powershell.exe -NoProfile -NonInteractive -Command "(New-Object System.Security.Principal.NTAccount($env:USERNAME)).Translate([System.Security.Principal.SecurityIdentifier]).Value"`,
        { windowsHide: true },
        (_err, stdout) => resolve(stdout.trim())
      )
    })

    // Try 1: HKLM AccountPicture\Users\<SID> (Win11 MS account location)
    if (sid) {
      const regDump: string = await new Promise(resolve => {
        exec(
          `powershell.exe -NoProfile -NonInteractive -Command "try{$k=Get-Item 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\${sid}' -EA Stop;$k.GetValueNames()|%%{$_+'='+$k.GetValue($_)}}catch{'MISSING'}"`,
          { windowsHide: true },
          (_err, stdout) => resolve(stdout.trim())
        )
      })
      const imgPaths = regDump.match(/^Image\d+=(.+)$/gm)
        ?.map(l => l.replace(/^Image\d+=/, '').trim())
        .filter(p => p && fs.existsSync(p))
        .sort((a, b) => fs.statSync(b).size - fs.statSync(a).size) ?? []
      for (const p of imgPaths) {
        avatar = readImg(p)
        if (avatar) break
      }
    }

    // Try 2: C:\Users\Public\AccountPictures\<SID>\ (Win11 file cache)
    if (!avatar && sid) {
      try {
        const dir = `C:\\Users\\Public\\AccountPictures\\${sid}`
        const allFiles: string[] = fs.existsSync(dir) ? fs.readdirSync(dir) : []
        const imgs = allFiles
          .filter((f: string) => /\.(png|jpg|jpeg|bmp|webp)$/i.test(f))
          .sort((a: string, b: string) => (parseInt(b.match(/(\d+)/)?.[1] ?? '0')) - (parseInt(a.match(/(\d+)/)?.[1] ?? '0')))
        for (const f of imgs) { avatar = readImg(path.join(dir, f)); if (avatar) break }
      } catch { /**/ }
    }

    // Try 3: ProgramData user-* files (largest numbered first)
    if (!avatar) {
      try {
        const dir = 'C:\\ProgramData\\Microsoft\\User Account Pictures'
        const allFiles: string[] = fs.existsSync(dir) ? fs.readdirSync(dir) : []
        const userImgs = allFiles
          .filter((f: string) => /^user[\-.].*\.(png|jpg|jpeg|bmp|webp)$/i.test(f))
          .sort((a: string, b: string) => (parseInt(b.match(/(\d+)/)?.[1] ?? '0')) - (parseInt(a.match(/(\d+)/)?.[1] ?? '0')))
        for (const f of userImgs) { avatar = readImg(path.join(dir, f)); if (avatar) break }
      } catch { /**/ }
    }

    return { avatar, name }
  })

  // ── IPC: System actions ────────────────────────────────────────────────────

  ipcMain.handle('system-action', (_e, action: string) => {
    switch (action) {
      case 'lock': exec('rundll32.exe user32.dll,LockWorkStation', { windowsHide: true }); break
      case 'sleep': exec('rundll32.exe powrprof.dll,SetSuspendState 0,1,0', { windowsHide: true }); break
      case 'restart': exec('shutdown /r /t 0', { windowsHide: true }); break
      case 'shutdown': exec('shutdown /s /t 0', { windowsHide: true }); break
      case 'screenshot': exec('explorer.exe ms-screenclip:', { windowsHide: true }); break
      case 'settings': exec('explorer.exe ms-settings:', { windowsHide: true }); break
      case 'profile': exec('explorer.exe ms-settings:yourinfo', { windowsHide: true }); break
      default:
        if (action.startsWith('launch:')) {
          const appId = action.slice(7)
          if (appId.includes('!')) exec(`explorer.exe shell:AppsFolder\\${appId}`, { windowsHide: true })
          else exec(`explorer.exe "${appId}"`, { windowsHide: true })
        }
        break
    }
  })

  // ── Island hit-box + interaction interval ──────────────────────────────────

  let hoverActive: any = false
  let interactActive: any = false
  let taskbarInteractActive: any = true
  let hitBox = { w: 160, h: 32 }
  let drawerOpen = false

  ipcMain.on('set-hit-box', (_e, w: number, h: number) => { hitBox = { w, h } })

  let taskbarNeedsReassert = false
  let islandNeedsReassert  = false

  setInterval(() => {
    const { x, y } = screen.getCursorScreenPoint()
    const bounds = islandWin.getBounds()
    const centerX = bounds.x + bounds.width / 2
    const overIsland = x >= centerX - hitBox.w / 2 && x <= centerX + hitBox.w / 2 && y >= bounds.y && y <= bounds.y + hitBox.h

    if (overIsland && islandNeedsReassert && !islandWin.isDestroyed()) {
      islandNeedsReassert = false
      const b = islandWin.getBounds()
      islandWin.setBounds({ width: b.width + 1 })
      islandWin.setBounds(b)
    }

    // Always force-set ignore state — no delta guard — self-heals after lock/unlock/fullscreen
    islandWin.setIgnoreMouseEvents(!overIsland, { forward: true })
    interactActive = overIsland
    if (overIsland !== hoverActive) { hoverActive = overIsland; islandWin.webContents.send('island:hover', overIsland) }

    if (!taskbarWin.isDestroyed()) {
      const tb = taskbarWin.getBounds()
      const overTb = y >= tb.y && y <= tb.y + TASKBAR_H
      if (overTb && taskbarNeedsReassert) {
        taskbarNeedsReassert = false
        taskbarWin.setBounds({ width: tb.width + 1 })
        taskbarWin.setBounds(tb)
      }

      taskbarWin.setIgnoreMouseEvents(!overTb, { forward: true })
      taskbarInteractActive = overTb
    }

    if (!notifWin.isDestroyed() && notifWin.isVisible()) {
      const nb = notifWin.getBounds()
      const overNotif = x >= nb.x && x <= nb.x + nb.width && y >= nb.y && y <= nb.y + nb.height
      notifWin.setIgnoreMouseEvents(!overNotif, { forward: true })
    }
  }, 16)

  // After Windows lock/unlock/fullscreen, DWM caches hit-test bounds for transparent
  // windows. We flag them here, and the poll loop will apply the 1px resize trick
  // EXACTLY when the user first hovers over them. This ensures DWM is ready and
  // reduces the visual jitter to a single frame that blends with hover animations.
  function reassertWindows(tag?: string) {
    taskbarNeedsReassert = true
    islandNeedsReassert  = true

    if (!taskbarWin.isDestroyed()) {
      taskbarWin.setAlwaysOnTop(true, 'pop-up-menu')
      taskbarWin.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })
      taskbarWin.setOpacity(1)
    }

    if (!islandWin.isDestroyed()) {
      islandWin.setAlwaysOnTop(true, 'pop-up-menu')
      islandWin.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })
      islandWin.moveTop()
    }

    interactActive = null as any
    hoverActive = null as any
    taskbarInteractActive = null as any
  }

  const onUnlock = () => reassertWindows('unlock')
  const onResume = () => reassertWindows('resume')

  powerMonitor.on('unlock-screen', onUnlock)
  powerMonitor.on('resume', onResume)

  ipcMain.on('set-ignore-mouse', (_e, ignore: boolean) => islandWin.setIgnoreMouseEvents(ignore, { forward: true }))
  ipcMain.on('control-media', (_e, action: 'play-pause' | 'next' | 'prev', sourceAppId: string) => controlMedia(action, sourceAppId))
  ipcMain.on('switch-virtual-desktop', (_e, idx: number) => switchVirtualDesktop(idx))

  // ── Display change handling ────────────────────────────────────────────────

  const handleDisplayChange = () => {
    const width = appBarRect.right - appBarRect.left
    taskbarWin.setBounds({ x: appBarRect.left, y: appBarRect.top, width: width > 0 ? width : taskbarWin.getBounds().width, height: TASKBAR_H })
    const { bounds } = screen.getPrimaryDisplay()
    islandWin.setBounds({ x: bounds.x + Math.floor((bounds.width - islandWin.getBounds().width) / 2), y: bounds.y, width: islandWin.getBounds().width, height: islandWin.getBounds().height })
    syncDrawerPosition()
    syncNotifPosition()
  }
  screen.on('display-metrics-changed', handleDisplayChange)
  screen.on('display-added', handleDisplayChange)
  screen.on('display-removed', handleDisplayChange)

  // ── Load renderer URLs ─────────────────────────────────────────────────────

  if (process.env['ELECTRON_RENDERER_URL']) {
    taskbarWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=taskbar`)
    islandWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=island`)
    drawerWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=drawer`)
    notifWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=notifications`)
  } else {
    taskbarWin.loadFile('out/renderer/index.html', { search: 'view=taskbar' })
    islandWin.loadFile('out/renderer/index.html', { search: 'view=island' })
    drawerWin.loadFile('out/renderer/index.html', { search: 'view=drawer' })
    notifWin.loadFile('out/renderer/index.html', { search: 'view=notifications' })
  }

  // ── Cleanup ────────────────────────────────────────────────────────────────

  let cleanedUp = false
  const cleanup = () => {
    if (cleanedUp) return; cleanedUp = true
    screen.off('display-metrics-changed', handleDisplayChange)
    screen.off('display-added', handleDisplayChange)
    screen.off('display-removed', handleDisplayChange)
    powerMonitor.off('unlock-screen', onUnlock)
    powerMonitor.off('resume', onResume)
    systemPreferences.off('accent-color-changed', onAccentChanged)
    detachAppBar()
  }

  app.on('before-quit', cleanup); app.on('will-quit', cleanup)
  taskbarWin.on('closed', cleanup); islandWin.on('closed', cleanup)
  process.once('SIGINT', () => { cleanup(); process.exit(0) })
  process.once('SIGTERM', () => { cleanup(); process.exit(0) })
  process.once('uncaughtException', e => { console.error(e); cleanup(); process.exit(1) })
  process.once('unhandledRejection', r => { console.error(r); cleanup(); process.exit(1) })
})

app.on('window-all-closed', () => app.quit())
