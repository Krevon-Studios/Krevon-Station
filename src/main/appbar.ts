import { execFileSync, spawn, type ChildProcessWithoutNullStreams } from 'child_process'
import { existsSync, readFileSync, rmSync } from 'fs'
import { join } from 'path'
import { app, BrowserWindow, screen } from 'electron'
import { TASKBAR_H } from './window'

type Rect = { left: number; top: number; right: number; bottom: number }
type PersistedState = { originalWorkArea: Rect; reservedWorkArea: Rect }

const LEGACY_STATE_FILE = join(app.getPath('userData'), 'native-shell', 'workarea-state.json')

function restoreLegacyWorkAreaIfNeeded(): void {
  if (!existsSync(LEGACY_STATE_FILE)) return

  try {
    const persisted = JSON.parse(readFileSync(LEGACY_STATE_FILE, 'utf8')) as PersistedState
    const original = persisted.originalWorkArea
    execFileSync(
      'powershell.exe',
      [
        '-NonInteractive',
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-Command',
        [
          'Add-Type -TypeDefinition @"',
          'using System;',
          'using System.Runtime.InteropServices;',
          'public static class DynamicIslandWorkArea {',
          '  [StructLayout(LayoutKind.Sequential)]',
          '  public struct RECT { public int left; public int top; public int right; public int bottom; }',
          '  [DllImport("user32.dll", SetLastError=true)]',
          '  public static extern bool SystemParametersInfo(uint uiAction, uint uiParam, ref RECT pvParam, uint fWinIni);',
          '}',
          '"@',
          '$SPI_SETWORKAREA = 0x002F',
          '$SPIF_SENDCHANGE = 0x0002',
          '$rect = New-Object DynamicIslandWorkArea+RECT',
          `$rect.left = ${original.left}`,
          `$rect.top = ${original.top}`,
          `$rect.right = ${original.right}`,
          `$rect.bottom = ${original.bottom}`,
          '[void][DynamicIslandWorkArea]::SystemParametersInfo($SPI_SETWORKAREA, 0, [ref]$rect, $SPIF_SENDCHANGE)',
        ].join('\n'),
      ],
      { stdio: 'ignore', timeout: 5000 }
    )
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    console.warn(`[appbar] failed to restore legacy work area: ${message}`)
  } finally {
    try {
      rmSync(LEGACY_STATE_FILE, { force: true })
    } catch {
      // Ignore cleanup failures.
    }
  }
}

function getHelperSourcePath(): string {
  return join(__dirname, '../../src/main/vendor/AppBarHelper.source.cs')
}

function getHelperExePath(): string {
  return join(__dirname, '../../src/main/vendor/AppBarHelper.exe')
}

function ensureHelperBuilt(): string {
  const exePath = getHelperExePath()
  if (existsSync(exePath)) return exePath

  const sourcePath = getHelperSourcePath()
  const compilers = [
    'C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe',
    'C:\\Windows\\Microsoft.NET\\Framework\\v4.0.30319\\csc.exe',
  ]

  const compiler = compilers.find((path) => existsSync(path))
  if (!compiler) {
    throw new Error('Windows C# compiler not found. Expected csc.exe in .NET Framework v4.0.30319.')
  }

  execFileSync(
    compiler,
    [
      '/nologo',
      '/target:winexe',
      '/optimize+',
      '/platform:anycpu',
      `/out:${exePath}`,
      '/reference:System.dll',
      '/reference:System.Core.dll',
      '/reference:System.Drawing.dll',
      '/reference:System.Windows.Forms.dll',
      sourcePath,
    ],
    { stdio: 'inherit', timeout: 20000 }
  )

  return exePath
}

export function attachAppBar(
  _win: BrowserWindow,
  onRect?: (rect: Rect) => void,
  onFullscreen?: (isFullscreen: boolean) => void
): () => void {
  restoreLegacyWorkAreaIfNeeded()

  let helper: ReturnType<typeof spawn> | null = null

  try {
    const helperExe = ensureHelperBuilt()
    const proc = spawn(
      helperExe,
      [`/edge:top`, `/height:${TASKBAR_H}`, `/parentPid:${process.pid}`],
      {
        windowsHide: true,
        stdio: ['ignore', 'pipe', 'pipe'] as const,
      }
    )
    helper = proc

    proc.stdout.on('data', (data: Buffer) => {
      for (const line of data.toString().trim().split(/\r?\n/)) {
        // FULLSCREEN notification from the C# helper
        const fsMatch = line.match(/^FULLSCREEN\|(\d)$/)
        if (fsMatch) {
          onFullscreen?.(fsMatch[1] === '1')
          continue
        }

        // Rect notification: left|top|right|bottom (physical pixels)
        const rectMatch = line.match(/^(-?\d+)\|(-?\d+)\|(-?\d+)\|(-?\d+)$/)
        if (!rectMatch) continue
        // The helper is now PerMonitorV2 DPI-aware, so the rect it emits is in
        // physical pixels. Electron's setBounds() takes DIPs, so we divide by
        // the primary display's scaleFactor to convert.
        const sf = screen.getPrimaryDisplay().scaleFactor
        onRect?.({
          left:   Math.round(Number(rectMatch[1]) / sf),
          top:    Math.round(Number(rectMatch[2]) / sf),
          right:  Math.round(Number(rectMatch[3]) / sf),
          bottom: Math.round(Number(rectMatch[4]) / sf),
        })
      }
    })

    proc.stderr.on('data', (data: Buffer) => {
      const message = data.toString().trim()
      if (message) console.warn(`[appbar] helper stderr: ${message}`)
    })

    proc.on('exit', (code, signal) => {
      if (code !== 0 && code !== null) {
        console.warn(`[appbar] helper exited with code ${code}${signal ? ` (${signal})` : ''}`)
      }
      helper = null
    })

    proc.on('error', (error) => {
      console.warn(`[appbar] helper failed: ${error.message}`)
      helper = null
    })
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    console.warn(`[appbar] unable to start helper: ${message}`)
  }

  return () => {
    if (!helper || helper.killed) return
    try {
      helper.kill()
    } catch {
      // Ignore shutdown failures; helper also watches the parent PID.
    }
  }
}
