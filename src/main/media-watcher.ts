import { Worker } from 'worker_threads'
import { execFile } from 'child_process'
import { writeFileSync } from 'fs'
import { tmpdir } from 'os'
import { join } from 'path'
import type { BrowserWindow } from 'electron'

// ── Per-session media control ─────────────────────────────────────────────────
//
// Priority:
//  1. WinRT TryTogglePlayPauseAsync/TrySkipNextAsync/TrySkipPreviousAsync on the
//     specific session — the ONLY approach that works for UWP apps (Spotify, etc.)
//     Poll IAsyncInfo.Status (0=Started,1=Completed) to wait without AsTask<T>.
//  2. PostMessage(WM_APPCOMMAND) to app window — fallback for Win32 apps (Chrome).
//
const CONTROL_PS1 = [
  'param([string]$sourceAppId, [string]$action)',
  'try {',
  // ── 1. WinRT direct session control ──
  '    Add-Type -AssemblyName System.Runtime.WindowsRuntime',
  '    [void][Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager,Windows.Media.Control,ContentType=WindowsRuntime]',
  '    $asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq "AsTask" -and $_.GetParameters().Count -eq 1 })[0]',
  '    $mgrTask = $asTask.MakeGenericMethod([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]).Invoke($null,',
  '        @([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()))',
  '    $mgrTask.Wait()',
  '    $mgr = $mgrTask.Result',
  '    $session = $mgr.GetSessions() | Where-Object { $_.SourceAppUserModelId -eq $sourceAppId } | Select-Object -First 1',
  '    if (-not $session) { $session = $mgr.GetCurrentSession() }',
  '    if ($session) {',
  '        $op = switch ($action) {',
  '            "play-pause" { $session.TryTogglePlayPauseAsync() }',
  '            "next"       { $session.TrySkipNextAsync()        }',
  '            "prev"       { $session.TrySkipPreviousAsync()    }',
  '            default      { $null }',
  '        }',
  '        if ($op) {',
  // Poll IAsyncInfo.Status: 0=Started, 1=Completed, 2=Canceled, 3=Error
  '            $dl = (Get-Date).AddMilliseconds(1500)',
  '            while ([int]$op.Status -eq 0 -and (Get-Date) -lt $dl) {',
  '                [System.Threading.Thread]::Sleep(15)',
  '            }',
  '            exit 0',
  '        }',
  '    }',
  '} catch { }',
  // ── 2. Win32 fallback: PostMessage(WM_APPCOMMAND) ──
  'try {',
  '    Add-Type -TypeDefinition @"',
  'using System;using System.Runtime.InteropServices;',
  'public class MC2 { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l); }',
  '"@',
  '    $cmd = switch ($action) { "play-pause" {14} "next" {11} "prev" {12} default {0} }',
  '    if ($cmd -eq 0) { exit 0 }',
  '    $lp  = [IntPtr]($cmd * 65536)',
  '    $pn  = ($sourceAppId -replace ".*!", "") -replace "\\.exe$", ""',
  '    foreach ($p in (Get-Process -Name $pn -ErrorAction SilentlyContinue)) {',
  '        if ($p.MainWindowHandle -ne [IntPtr]::Zero) {',
  '            [MC2]::PostMessage($p.MainWindowHandle, 0x0319, $p.MainWindowHandle, $lp) | Out-Null',
  '            break',
  '        }',
  '    }',
  '} catch { }',
].join('\n')

const CONTROL_SCRIPT = join(tmpdir(), 'di-control.ps1')
try { writeFileSync(CONTROL_SCRIPT, CONTROL_PS1, 'utf8') } catch { /* ignore */ }

export function controlMedia(
  action: 'play-pause' | 'next' | 'prev',
  sourceAppId: string
): void {
  execFile(
    'powershell.exe',
    ['-NonInteractive', '-NoProfile', '-ExecutionPolicy', 'Bypass',
     '-File', CONTROL_SCRIPT, '-sourceAppId', sourceAppId, '-action', action],
    { timeout: 5000 },
    (err) => { if (err) console.warn('[media-control]', err.message) }
  )
}

// ── Monitor worker ───────────────────────────────────────────────────────────

export function startMediaWatcher(win: BrowserWindow): void {
  const workerPath = join(__dirname, 'media-worker.js')
  let worker: Worker | null = null

  function start() {
    try { worker = new Worker(workerPath) } catch (err) {
      console.warn('[media-watcher] failed to start worker:', err); return
    }

    worker.on('message', (data: Record<string, unknown>) => {
      if (win.isDestroyed()) return
      if (data.__status) { console.log('[media-watcher] ready'); return }
      if (data.__error)  { console.warn('[media-watcher] error:', data.__error); return }
      win.webContents.send('island:media', data)
    })

    worker.on('error', (err) => console.warn('[media-watcher] threw:', err.message))
    worker.on('exit',  (code) => { if (code !== 0) console.warn('[media-watcher] exit', code); worker = null })
  }

  setTimeout(start, 1500)

  win.on('closed', () => { worker?.postMessage('destroy'); worker?.terminate(); worker = null })
}
