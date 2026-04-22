import { Worker } from 'worker_threads'
import { execFile } from 'child_process'
import { writeFileSync } from 'fs'
import { tmpdir } from 'os'
import { join } from 'path'
import type { BrowserWindow } from 'electron'

// ── Per-session media control ─────────────────────────────────────────────────
//
// Priority:
//  1. WinRT TryTogglePlayPauseAsync/TrySkipNextAsync/TrySkipPreviousAsync — works
//     for UWP/Store apps (Spotify). AsTask<T> overload selected precisely by
//     IsGenericMethod + IAsyncOperation`1 param type to avoid picking IAsyncAction.
//  2. PostMessage(WM_APPCOMMAND) — fallback for Win32 apps (Chrome, etc).
//
const CONTROL_PS1 = [
  'param([string]$sourceAppId, [string]$action)',
  '$winrtOk = $false',
  'try {',
  '    Add-Type -AssemblyName System.Runtime.WindowsRuntime',
  '    [void][Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager,Windows.Media.Control,ContentType=WindowsRuntime]',
  // Select AsTask<T>(IAsyncOperation<T>) — must check IsGenericMethod AND param type name
  // to avoid accidentally picking the IAsyncAction overload (also 1 param, not generic)
  '    $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {',
  '        $_.Name -eq "AsTask" -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and',
  '        $_.GetParameters()[0].ParameterType.Name -eq "IAsyncOperation``1"',
  '    } | Select-Object -First 1',
  '    $mgrTask = $asTask.MakeGenericMethod([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]).Invoke($null,',
  '        @([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()))',
  '    $mgrTask.Wait() | Out-Null',
  '    $mgr = $mgrTask.Result',
  '    $session = $mgr.GetSessions() | Where-Object { $_.SourceAppUserModelId -eq $sourceAppId } | Select-Object -First 1',
  '    if (-not $session) { $session = $mgr.GetSessions() | Where-Object { ($_.SourceAppUserModelId -like "*$sourceAppId*") -or ($sourceAppId -like "*$($_.SourceAppUserModelId)*") } | Select-Object -First 1 }',
  '    if (-not $session) { $session = $mgr.GetCurrentSession() }',
  '    if ($session) {',
  '        $op = switch ($action) {',
  '            "play-pause" { $session.TryTogglePlayPauseAsync() }',
  '            "next"       { $session.TrySkipNextAsync()        }',
  '            "prev"       { $session.TrySkipPreviousAsync()    }',
  '            default      { $null }',
  '        }',
  '        if ($op) {',
  '            $opTask = $asTask.MakeGenericMethod([System.Boolean]).Invoke($null, @($op))',
  '            $opTask.Wait([System.Threading.Timeout]::Infinite) | Out-Null',
  '            if ($opTask.Result -eq $true) { $winrtOk = $true }',
  '        }',
  '    }',
  '} catch { }',
  'if ($winrtOk) { exit 0 }',
  // ── 2. Win32 fallback: PostMessage(WM_APPCOMMAND) ──
  'try {',
  '    Add-Type -TypeDefinition @"',
  'using System;using System.Runtime.InteropServices;',
  'public class MC2 { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l); }',
  '"@',
  '    $cmd = switch ($action) { "play-pause" {14} "next" {11} "prev" {12} default {0} }',
  '    if ($cmd -eq 0) { exit 0 }',
  '    $lp  = [IntPtr]($cmd * 65536)',
  '    $pn  = ($sourceAppId -replace ".*!", "") -replace ".*[\\\\/]", "" -replace "\\.exe$", ""',
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
  console.log('[media-control] action=%s sourceAppId=%s', action, sourceAppId)
  execFile(
    'powershell.exe',
    ['-NonInteractive', '-NoProfile', '-ExecutionPolicy', 'Bypass',
     '-File', CONTROL_SCRIPT, '-sourceAppId', sourceAppId, '-action', action],
    { timeout: 5000 },
    (err, stdout, stderr) => {
      if (stdout) console.log('[media-control] stdout:', stdout.trim())
      if (stderr) console.warn('[media-control] stderr:', stderr.trim())
      if (err)    console.warn('[media-control] err:', err.message)
    }
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
