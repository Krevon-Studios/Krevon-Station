import { spawn, execSync, ChildProcessWithoutNullStreams } from 'child_process'
import { app, BrowserWindow, ipcMain } from 'electron'
import { join } from 'path'

let desktopCount = 1
let activeDesktopIndex = 0
let switchPs: ChildProcessWithoutNullStreams | null = null
let mainWin: BrowserWindow | null = null
let switchScriptPath: string | null = null

function readInitialState() {
  try {
    const stdout = execSync(
      'reg query "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops"',
      { encoding: 'utf-8' }
    )
    let idsStr = ''
    let currentStr = ''
    for (const line of stdout.split('\n')) {
      if (line.includes('VirtualDesktopIDs')) {
        const m = line.match(/REG_BINARY\s+([A-Fa-f0-9]+)/)
        if (m) idsStr = m[1]
      }
      if (line.includes('CurrentVirtualDesktop')) {
        const m = line.match(/REG_BINARY\s+([A-Fa-f0-9]+)/)
        if (m) currentStr = m[1]
      }
    }
    if (idsStr) {
      desktopCount = Math.max(1, idsStr.length / 32)
      if (currentStr) {
        const idx = idsStr.indexOf(currentStr)
        if (idx !== -1) activeDesktopIndex = idx / 32
      }
    }
  } catch {
    // keep defaults
  }
}

export function startDesktopWatcher(win: BrowserWindow) {
  mainWin = win

  const monitorScriptPath = app.isPackaged
    ? join(process.resourcesPath, 'desktop-monitor.ps1')
    : join(__dirname, '../../src/main/desktop-monitor.ps1')
  switchScriptPath = app.isPackaged
    ? join(process.resourcesPath, 'switch-desktop.ps1')
    : join(__dirname, '../../src/main/switch-desktop.ps1')

  // Read state synchronously before anything else
  readInitialState()

  // IPC handle: renderer pulls current state on mount (avoids did-finish-load race)
  ipcMain.handle('get-virtual-desktops', () => ({
    count: desktopCount,
    activeIndex: activeDesktopIndex
  }))

  startSwitchProcess()

  // Registry change monitor
  const ps = spawn('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', monitorScriptPath])

  ps.stdout.on('data', (data: Buffer) => {
    for (const line of data.toString().trim().split(/\r?\n/)) {
      if (!line) continue
      const parts = line.trim().split('|')
      if (parts.length !== 2) continue

      const idsStr = parts[0]
      const currentStr = parts[1]
      if (!idsStr) continue

      const count = Math.max(1, idsStr.length / 32)
      let activeIdx = 0
      if (currentStr) {
        const idx = idsStr.indexOf(currentStr)
        if (idx !== -1) activeIdx = idx / 32
      }

      if (count !== desktopCount || activeIdx !== activeDesktopIndex) {
        desktopCount = count
        activeDesktopIndex = activeIdx
        win.webContents.send('island:virtual-desktops', { count, activeIndex: activeIdx })
      }
    }
  })

  ps.on('error', () => {
    win.webContents.send('island:virtual-desktops', { count: 1, activeIndex: 0 })
  })
}

export function switchVirtualDesktop(targetIndex: number) {
  if (targetIndex === activeDesktopIndex || targetIndex >= desktopCount || targetIndex < 0) return

  const from = activeDesktopIndex

  // Optimistic push → UI responds immediately, no waiting for registry event
  activeDesktopIndex = targetIndex
  mainWin?.webContents.send('island:virtual-desktops', { count: desktopCount, activeIndex: targetIndex })

  if (!switchPs || switchPs.killed || switchPs.exitCode !== null) {
    startSwitchProcess()
  }

  if (switchPs?.stdin && !switchPs.stdin.destroyed) {
    switchPs.stdin.write(`${targetIndex}|${from}\n`)
  }
}

function startSwitchProcess() {
  if (!switchScriptPath || (switchPs && !switchPs.killed && switchPs.exitCode === null)) return

  switchPs = spawn('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', switchScriptPath])

  switchPs.stderr.on('data', (data: Buffer) => {
    const message = data.toString().trim()
    if (message) {
      console.warn(`[desktop-watcher] ${message}`)
    }
  })

  switchPs.on('exit', (code, signal) => {
    if (code !== 0 && code !== null) {
      console.warn(`[desktop-watcher] switch process exited with code ${code}${signal ? ` (${signal})` : ''}`)
    }
    switchPs = null
  })

  switchPs.on('error', (error) => {
    console.warn('[desktop-watcher] failed to start switch process:', error)
    switchPs = null
  })
}
