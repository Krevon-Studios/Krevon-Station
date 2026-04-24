import { app, ipcMain, screen } from 'electron'
import { createIslandWindow, createTaskbarWindow, createDrawerWindow, TASKBAR_H } from './window'
import { startHookServer } from './hook-server'
import { startMediaWatcher, controlMedia } from './media-watcher'
import { startDesktopWatcher, switchVirtualDesktop } from './desktop-watcher'
import { createTray } from './tray'
import { attachAppBar } from './appbar'
import { startSystemStatsWatcher, getCachedSystemStats, setAudioVolume, setAudioMute, requestAudioSessions, setSessionVolume, requestAudioDevices, setAudioDevice, scriptPath } from './system-stats'
import { spawn, exec } from 'child_process'

app.setName('Dynamic Island')

if (!app.requestSingleInstanceLock()) {
  app.quit()
  process.exit(0)
}

// ─────────────────────────────────────────────────────────────────────────────

app.whenReady().then(() => {
  const taskbarWin = createTaskbarWindow()
  const islandWin  = createIslandWindow()
  const drawerWin  = createDrawerWindow()
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
        islandWin.setOpacity(0);  islandWin.setIgnoreMouseEvents(true, { forward: true })
        if (!drawerWin.isDestroyed() && drawerOpen) {
          drawerOpen = false
          drawerWin.setIgnoreMouseEvents(true, { forward: true })
          drawerWin.webContents.send('drawer:force-close')
        }
      } else {
        taskbarWin.setOpacity(1); islandWin.setOpacity(1)
        interactActive = -1 as any; hoverActive = -1 as any; taskbarInteractActive = -1 as any
      }
    }
  )

  islandWin.once('show', () => pinIslandToTop())
  startHookServer(islandWin)
  startMediaWatcher(islandWin)
  startDesktopWatcher(taskbarWin)
  startSystemStatsWatcher([taskbarWin, drawerWin])
  createTray([taskbarWin, islandWin])

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

  // ── IPC: System stats ──────────────────────────────────────────────────────

  ipcMain.handle('get-system-stats', () => getCachedSystemStats())

  // ── IPC: Drawer ────────────────────────────────────────────────────────────

  ipcMain.on('drawer:open', (_e, type: string) => {
    if (drawerWin.isDestroyed()) return
    drawerOpen = true
    syncDrawerPosition()
    drawerWin.webContents.send('drawer:show', type)
    drawerWin.focus()
  })

  drawerWin.on('blur', () => {
    if (drawerOpen && !drawerWin.isDestroyed()) {
      drawerWin.webContents.send('drawer:force-close')
    }
  })

  // Sent by taskbar/external callers — tells React to animate-close, which then sends drawer:close
  ipcMain.on('drawer:request-close', () => {
    if (!drawerWin.isDestroyed()) drawerWin.webContents.send('drawer:force-close')
  })

  // Sent by React after its close animation finishes
  ipcMain.on('drawer:close', () => {
    drawerOpen = false
    taskbarWin.webContents.send('drawer:closed')
  })

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

  // ── Island hit-box + interaction interval ──────────────────────────────────

  let hoverActive: any = false
  let interactActive: any = false
  let taskbarInteractActive: any = true
  let hitBox = { w: 160, h: 32 }
  let drawerOpen = false

  ipcMain.on('set-hit-box', (_e, w: number, h: number) => { hitBox = { w, h } })

  setInterval(() => {
    const { x, y } = screen.getCursorScreenPoint()
    const bounds = islandWin.getBounds()
    const centerX = bounds.x + bounds.width / 2
    const overIsland = x >= centerX - hitBox.w / 2 && x <= centerX + hitBox.w / 2 && y >= bounds.y && y <= bounds.y + hitBox.h

    if (overIsland !== interactActive) { interactActive = overIsland; islandWin.setIgnoreMouseEvents(!overIsland, { forward: true }) }
    if (overIsland !== hoverActive)    { hoverActive = overIsland;    islandWin.webContents.send('island:hover', overIsland) }

    if (!taskbarWin.isDestroyed()) {
      const tb = taskbarWin.getBounds()
      const overTb = y >= tb.y && y <= tb.y + TASKBAR_H
      if (overTb !== taskbarInteractActive) { taskbarInteractActive = overTb; taskbarWin.setIgnoreMouseEvents(!overTb, { forward: true }) }
    }
  }, 16)

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
  }
  screen.on('display-metrics-changed', handleDisplayChange)
  screen.on('display-added', handleDisplayChange)
  screen.on('display-removed', handleDisplayChange)

  // ── Load renderer URLs ─────────────────────────────────────────────────────

  if (process.env['ELECTRON_RENDERER_URL']) {
    taskbarWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=taskbar`)
    islandWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=island`)
    drawerWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=drawer`)
  } else {
    taskbarWin.loadFile('out/renderer/index.html', { search: 'view=taskbar' })
    islandWin.loadFile('out/renderer/index.html', { search: 'view=island' })
    drawerWin.loadFile('out/renderer/index.html', { search: 'view=drawer' })
  }

  // ── Cleanup ────────────────────────────────────────────────────────────────

  let cleanedUp = false
  const cleanup = () => {
    if (cleanedUp) return; cleanedUp = true
    screen.off('display-metrics-changed', handleDisplayChange)
    screen.off('display-added', handleDisplayChange)
    screen.off('display-removed', handleDisplayChange)
    detachAppBar()
  }

  app.on('before-quit', cleanup); app.on('will-quit', cleanup)
  taskbarWin.on('closed', cleanup); islandWin.on('closed', cleanup)
  process.once('SIGINT',  () => { cleanup(); process.exit(0) })
  process.once('SIGTERM', () => { cleanup(); process.exit(0) })
  process.once('uncaughtException',  e => { console.error(e); cleanup(); process.exit(1) })
  process.once('unhandledRejection', r => { console.error(r); cleanup(); process.exit(1) })
})

app.on('window-all-closed', () => app.quit())
