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

export type NetworkType = 'wifi' | 'ethernet' | 'none'

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

export function getCachedSystemStats(): SystemStats {
  return _cached
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

function scriptPath(name: string): string {
  return app.isPackaged
    ? path.join(process.resourcesPath, name)
    : path.join(app.getAppPath(), 'src/main', name)
}

function spawnPython(
  script: string,
  label: string,
  onLine: (line: string) => void,
  onStop: () => void,
  exe = 'py'
): ChildProcessWithoutNullStreams {
  const proc = spawn(exe, [script], { windowsHide: true })

  proc.stderr.on('data', (d: Buffer) => {
    const msg = d.toString().trim()
    if (msg) console.error(`[${label}:err] ${msg}`)
  })

  createInterface({ input: proc.stdout, crlfDelay: Infinity })
    .on('line', onLine)

  proc.on('error', (err) => {
    if (exe === 'py') {
      setTimeout(() => spawnPython(script, label, onLine, onStop, 'python'), 300)
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

  function startAudio(exe = 'py') {
    if (stopped) return
    spawnPython(
      scriptPath('audio-monitor.py'), 'audio',
      (line) => {
        if (!line.startsWith('{')) return
        try {
          const d = JSON.parse(line)
          broadcast({ audio: {
            volume: Math.max(0, Math.min(100, Number(d.volume ?? 50))),
            muted:  Boolean(d.muted),
          }})
        } catch { /* ignore */ }
      },
      () => { if (!stopped) startAudio(exe) },
      exe
    )
  }

  // ── Network ────────────────────────────────────────────────────────────

  function startNetwork(exe = 'py') {
    if (stopped) return
    spawnPython(
      scriptPath('network-monitor.py'), 'network',
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
      () => { if (!stopped) startNetwork(exe) },
      exe
    )
  }

  startAudio()
  startNetwork()

  return () => { stopped = true }
}
