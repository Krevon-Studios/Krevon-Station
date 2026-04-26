/**
 * system-stats.ts
 *
 * Two Python child processes — both fully event-driven, zero polling:
 *
 *   audio-monitor.py   → IAudioEndpointVolumeCallback COM callback +
 *                         WaitForSingleObject (kernel event). CPU: ~0%
 *
 *   network-monitor.py → NotifyAddrChange (iphlpapi.dll) synchronous
 *                         blocking call. CPU: ~0% between changes.
 */

import { spawn, ChildProcessWithoutNullStreams } from 'child_process'
import { createInterface } from 'readline'
import path from 'path'
import { app } from 'electron'
import type { BrowserWindow } from 'electron'

// ─────────────────────────────────────────────────────────────────────────────
// Shared types
// ─────────────────────────────────────────────────────────────────────────────

export type NetworkType = 'wifi' | 'none'

export interface NetworkState {
  type: NetworkType
  signal: number | null
  hasInternet: boolean
  ssid: string | null
  vpnActive: boolean
}

export interface AudioState {
  volume: number
  muted: boolean
}

export interface SystemStats {
  network: NetworkState
  audio: AudioState
}

// ─────────────────────────────────────────────────────────────────────────────
// Cached state
// ─────────────────────────────────────────────────────────────────────────────

let _cached: SystemStats = {
  network: { type: 'none', signal: null, ssid: null, hasInternet: false, vpnActive: false },
  audio:   { volume: 50, muted: false },
}

let _audioProc: ReturnType<typeof spawn> | null = null

// Pending session request queue — resolved when Python responds with {"type":"sessions",...}
const _sessionQueue: Array<(sessions: AudioSessionInfo[]) => void> = []
// Pending device request queue — resolved when Python responds with {"type":"devices",...}
const _deviceQueue:  Array<(result: { devices: AudioDeviceInfo[]; activeId: string }) => void> = []

export interface AudioSessionInfo {
  pid:    number
  name:   string
  volume: number
  muted:  boolean
}

export interface AudioDeviceInfo {
  id:   string
  name: string
}

export function getCachedSystemStats(): SystemStats {
  return _cached
}

export function setAudioVolume(volume: number): void {
  if (_audioProc && !_audioProc.killed) {
    try { _audioProc.stdin?.write(JSON.stringify({ volume: Math.round(volume) }) + '\n') } catch { /* */ }
  }
}

export function setAudioMute(muted: boolean): void {
  if (_audioProc && !_audioProc.killed) {
    try { _audioProc.stdin?.write(JSON.stringify({ muted }) + '\n') } catch { /* */ }
  }
}

export function requestAudioSessions(): Promise<AudioSessionInfo[]> {
  return new Promise((resolve) => {
    const timer = setTimeout(() => {
      const idx = _sessionQueue.indexOf(resolve)
      if (idx !== -1) { _sessionQueue.splice(idx, 1); resolve([]) }
    }, 5000)

    _sessionQueue.push((sessions) => { clearTimeout(timer); resolve(sessions) })

    if (_audioProc && !_audioProc.killed) {
      try { _audioProc.stdin?.write(JSON.stringify({ cmd: 'sessions' }) + '\n') } catch {
        _sessionQueue.pop(); clearTimeout(timer); resolve([])
      }
    } else {
      _sessionQueue.pop(); clearTimeout(timer); resolve([])
    }
  })
}

export function setSessionVolume(pid: number, volume?: number, muted?: boolean): void {
  if (_audioProc && !_audioProc.killed) {
    try {
      _audioProc.stdin?.write(JSON.stringify({ session: { pid, volume, muted } }) + '\n')
    } catch { /* */ }
  }
}

export function requestAudioDevices(): Promise<{ devices: AudioDeviceInfo[]; activeId: string }> {
  return new Promise((resolve) => {
    const empty = { devices: [], activeId: '' }
    const timer = setTimeout(() => {
      const idx = _deviceQueue.indexOf(resolve)
      if (idx !== -1) { _deviceQueue.splice(idx, 1); resolve(empty) }
    }, 5000)

    _deviceQueue.push((result) => { clearTimeout(timer); resolve(result) })

    if (_audioProc && !_audioProc.killed) {
      try { _audioProc.stdin?.write(JSON.stringify({ cmd: 'devices' }) + '\n') } catch {
        _deviceQueue.pop(); clearTimeout(timer); resolve(empty)
      }
    } else {
      _deviceQueue.pop(); clearTimeout(timer); resolve(empty)
    }
  })
}

export function setAudioDevice(deviceId: string): void {
  if (_audioProc && !_audioProc.killed) {
    try { _audioProc.stdin?.write(JSON.stringify({ set_device: deviceId }) + '\n') } catch { /* */ }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

export function scriptPath(name: string): string {
  return app.isPackaged
    ? path.join(process.resourcesPath, name)
    : path.join(app.getAppPath(), 'src/main', name)
}

export function getPythonExe(): string {
  return app.isPackaged
    ? path.join(process.resourcesPath, 'python', 'python.exe')
    : 'py'
}

function spawnPython(
  scriptName: string, // e.g. 'audio-monitor'
  label: string,
  onLine: (line: string) => void,
  onStop: () => void,
  exeFallback = 'py'
): ChildProcessWithoutNullStreams {
  const isPackaged = app.isPackaged
  const exe = isPackaged
    ? path.join(process.resourcesPath, `${scriptName}.exe`)
    : exeFallback
  const args = isPackaged
    ? []
    : [path.join(app.getAppPath(), 'src/main', `${scriptName}.py`)]

  const proc = spawn(exe, args, { windowsHide: true })

  proc.stderr.on('data', (d: Buffer) => {
    const msg = d.toString().trim()
    if (msg) console.error(`[${label}:err] ${msg}`)
  })

  createInterface({ input: proc.stdout, crlfDelay: Infinity })
    .on('line', onLine)

  proc.on('error', (err) => {
    if (!isPackaged && exeFallback === 'py') {
      setTimeout(() => spawnPython(scriptName, label, onLine, onStop, 'python'), 300)
    } else {
      console.error(`[${label}] spawn error: ${err.message}`)
      setTimeout(onStop, 3000)
    }
  })

  proc.on('exit', (code) => {
    console.warn(`[${label}] exited (${code})`)
    setTimeout(onStop, 1000)
  })

  return proc
}

// ─────────────────────────────────────────────────────────────────────────────
// Watcher
// ─────────────────────────────────────────────────────────────────────────────

export function startSystemStatsWatcher(windows: BrowserWindow[]): () => void {
  let stopped = false

  function broadcast(patch: Partial<SystemStats>) {
    _cached = { ..._cached, ...patch }
    for (const win of windows) {
      if (!win.isDestroyed()) win.webContents.send('system-stats', _cached)
    }
  }

  // ── Audio ──────────────────────────────────────────────────────────────

  function startAudio(exeFallback = 'py') {
    if (stopped) return
    const proc = spawnPython(
      'audio-monitor', 'audio',
      (line) => {
        if (!line.startsWith('{')) return
        try {
          const d = JSON.parse(line)
          // Session list response
          if (d.type === 'sessions') {
            const cb = _sessionQueue.shift()
            if (cb) cb(d.sessions ?? [])
            return
          }
          // Device list response
          if (d.type === 'devices') {
            const cb = _deviceQueue.shift()
            if (cb) cb({ devices: d.devices ?? [], activeId: d.activeId ?? '' })
            return
          }
          // Master volume/mute state change
          broadcast({ audio: {
            volume: Math.max(0, Math.min(100, Number(d.volume ?? 50))),
            muted:  Boolean(d.muted),
          }})
        } catch { /* ignore */ }
      },
      () => { if (!stopped) startAudio(exeFallback) },
      exeFallback
    )
    _audioProc = proc
  }

  // ── Network ────────────────────────────────────────────────────────────

  function startNetwork(exeFallback = 'py') {
    if (stopped) return
    spawnPython(
      'network-monitor', 'network',
      (line) => {
        if (!line.startsWith('{')) return
        try {
          const d = JSON.parse(line)
          broadcast({ network: {
            type:        d.type        ?? 'none',
            signal:      d.signal      ?? null,
            ssid:        d.ssid        ?? null,
            hasInternet: Boolean(d.hasInternet),
            vpnActive:   Boolean(d.vpnActive),
          }})
        } catch { /* ignore */ }
      },
      () => { if (!stopped) startNetwork(exeFallback) },
      exeFallback
    )
  }

  startAudio()
  startNetwork()

  return () => { stopped = true }
}
