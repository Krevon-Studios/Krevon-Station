import { app, ipcMain, screen } from 'electron'
import { createIslandWindow, createTaskbarWindow, createDrawerWindow, TASKBAR_H } from './window'
import { startHookServer } from './hook-server'
import { startMediaWatcher, controlMedia } from './media-watcher'
import { startDesktopWatcher, switchVirtualDesktop } from './desktop-watcher'
import { createTray } from './tray'
import { attachAppBar } from './appbar'
import { startSystemStatsWatcher, getCachedSystemStats, setAudioVolume, setAudioMute, requestAudioSessions, setSessionVolume } from './system-stats'
import { spawn } from 'child_process'

app.setName('Dynamic Island')

if (!app.requestSingleInstanceLock()) {
  app.quit()
  process.exit(0)
}

// ── PowerShell helper ──────────────────────────────────────────────────────────

function runPS(cmd: string): Promise<string> {
  return new Promise((resolve) => {
    const proc = spawn('powershell', ['-NoProfile', '-NonInteractive', '-Command', cmd], { windowsHide: true })
    let out = ''
    proc.stdout.on('data', (d: Buffer) => (out += d.toString()))
    proc.on('close', () => resolve(out.trim()))
    proc.on('error', () => resolve(''))
  })
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
    drawerWin.setBounds({ x: bounds.x, y: bounds.y + TASKBAR_H, width: bounds.width, height: bounds.height - TASKBAR_H })
  }

  // ── IPC: System stats ──────────────────────────────────────────────────────

  ipcMain.handle('get-system-stats', () => getCachedSystemStats())

  // ── IPC: Drawer ────────────────────────────────────────────────────────────

  ipcMain.on('drawer:open', (_e, type: string) => {
    if (drawerWin.isDestroyed()) return
    drawerOpen = true
    syncDrawerPosition()
    drawerWin.webContents.send('drawer:show', type)
    drawerWin.setIgnoreMouseEvents(false)
  })

  // Sent by taskbar/external callers — tells React to animate-close, which then sends drawer:close
  ipcMain.on('drawer:request-close', () => {
    if (!drawerWin.isDestroyed()) drawerWin.webContents.send('drawer:force-close')
  })

  // Sent by React after its close animation finishes
  ipcMain.on('drawer:close', () => {
    drawerOpen = false
    if (!drawerWin.isDestroyed()) drawerWin.setIgnoreMouseEvents(true, { forward: true })
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

  ipcMain.handle('get-audio-devices', async () => {
    const out = await runPS(
      `Get-PnpDevice -Class AudioEndpoint -Status OK 2>$null ` +
      `| Where-Object { $_.FriendlyName -notmatch 'Microphone|Input|Capture|Headset Mic|Array' } ` +
      `| Select-Object -ExpandProperty FriendlyName`
    )
    const names   = out.split('\n').map(l => l.trim()).filter(Boolean)
    const devices = names.map((name, i) => ({ id: `dev-${i}`, name }))

    const activeOut = await runPS(
      `$type = [Type]::GetTypeFromCLSID([Guid]'BCDE0395-E52F-467C-8E3D-C4579291692E');\n` +
      `if ($type) { $e = [Activator]::CreateInstance($type); ($e.GetDefaultAudioEndpoint(0,1)).FriendlyName } else { '' }`
    )
    const activeId = devices.find(d => activeOut.includes(d.name))?.id ?? (devices[0]?.id ?? '')
    return { devices, activeId }
  })

  ipcMain.handle('get-audio-sessions', () => requestAudioSessions())

  ipcMain.handle('set-session-volume', (_e, pid: number, volume?: number, muted?: boolean) => {
    setSessionVolume(pid, volume, muted)
  })

  ipcMain.handle('set-audio-device', async (_e, _deviceId: string) => { /* future */ })

  // ── IPC: WiFi ─────────────────────────────────────────────────────────────

  ipcMain.handle('scan-wifi-networks', async () => {
    // No 2>$null — it expands $null in PS and mangles native cmd output
    const out = await runPS(`netsh wlan show networks mode=bssid`)
    const normalized = out.replace(/\r\n/g, '\n').replace(/\r/g, '\n')
    const connected  = getCachedSystemStats().network.ssid ?? ''
    const result: { ssid: string; signal: number; secured: boolean; connected: boolean }[] = []
    const seen = new Set<string>()

    // Find every "SSID N : <name>" header — variable whitespace
    const ssidMatches = [...normalized.matchAll(/^SSID\s+\d+\s*:\s*(.+)$/gm)]

    for (let i = 0; i < ssidMatches.length; i++) {
      const ssid = ssidMatches[i][1].trim()
      if (!ssid || seen.has(ssid)) continue

      // Extract the block between this SSID header and the next one
      const blockStart = ssidMatches[i].index!
      const blockEnd   = ssidMatches[i + 1]?.index ?? normalized.length
      const block      = normalized.slice(blockStart, blockEnd)

      const sigM  = block.match(/Signal\s*:\s*(\d+)%/)
      const authM = block.match(/Authentication\s*:\s*(.+)/)

      const signal  = sigM  ? parseInt(sigM[1])  : 50
      const auth    = authM ? authM[1].trim()     : 'WPA2-Personal'
      const secured = !auth.toLowerCase().includes('open')

      seen.add(ssid)
      result.push({ ssid, signal, secured, connected: ssid === connected })
    }

    return result.sort((a, b) => {
      if (a.connected !== b.connected) return a.connected ? -1 : 1
      return b.signal - a.signal
    })
  })

  ipcMain.handle('set-wifi-enabled', async (_e, enable: boolean) => {
    const nameOut = await runPS(`netsh wlan show interfaces | Select-String 'Name' | Select-Object -First 1`)
    const match   = nameOut.match(/Name\s*:\s*(.+)/)
    const adapter = match ? match[1].trim() : 'Wi-Fi'
    await runPS(`netsh interface set interface "${adapter}" ${enable ? 'enabled' : 'disabled'}`)
  })

  ipcMain.handle('get-wifi-state', async () => {
    const out = await runPS(`netsh wlan show interfaces`)
    if (!out || out.includes('no wireless interface')) return { enabled: false }
    return { enabled: out.includes('State') }
  })

  ipcMain.handle('connect-wifi', async (_e, ssid: string) => {
    await runPS(`netsh wlan connect name="${ssid}" 2>$null`)
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

    // Drawer: reinforce setIgnoreMouseEvents every tick — setBounds can reset Win32 state.
    if (!drawerWin.isDestroyed()) {
      if (drawerOpen) drawerWin.setIgnoreMouseEvents(false)
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
