/**
 * auto-update.ts
 *
 * Wires up electron-updater for GitHub Releases-based auto-update.
 * Only active in packaged builds — no-ops in dev mode.
 *
 * Update flow:
 *   1. On app ready → silently check for updates in background
 *   2. Update found → download automatically, show "downloading" dialog on manual check
 *   3. Download complete → prompt restart (dialog)
 *   4. Manual "Check for updates…" from tray always gives feedback at every state
 */

import { app, dialog, BrowserWindow } from 'electron'
import { autoUpdater } from 'electron-updater'

type UpdateState = 'idle' | 'checking' | 'downloading' | 'ready'

let _state: UpdateState = 'idle'
let _pendingVersion = ''
let _manualCheck = false

function activeWin(wins: BrowserWindow[]): BrowserWindow {
  return wins.find(w => !w.isDestroyed() && w.isVisible()) ?? wins[0]
}

export function initAutoUpdater(wins: BrowserWindow[]): void {
  if (!app.isPackaged) return

  autoUpdater.logger = null
  autoUpdater.autoDownload = true
  autoUpdater.autoInstallOnAppQuit = true

  autoUpdater.on('checking-for-update', () => {
    _state = 'checking'
  })

  autoUpdater.on('update-not-available', () => {
    _state = 'idle'
    if (_manualCheck) {
      _manualCheck = false
      dialog.showMessageBox(activeWin(wins), {
        type: 'info',
        title: 'No updates available',
        message: 'You\'re on the latest version.',
        detail: `Krevon Station ${app.getVersion()} is up to date.`,
        buttons: ['OK'],
      })
    }
  })

  autoUpdater.on('update-available', (info) => {
    _pendingVersion = info.version
    _state = 'downloading'
    console.log(`[updater] update available: ${info.version}`)
    if (_manualCheck) {
      _manualCheck = false
      dialog.showMessageBox(activeWin(wins), {
        type: 'info',
        title: 'Update found',
        message: `Krevon Station ${info.version} is downloading…`,
        detail: 'The update will install automatically. You\'ll be prompted to restart when it\'s ready.',
        buttons: ['OK'],
      })
    }
  })

  autoUpdater.on('download-progress', (progress) => {
    _state = 'downloading'
    console.log(`[updater] downloading: ${Math.round(progress.percent)}%`)
  })

  autoUpdater.on('update-downloaded', (info) => {
    _state = 'ready'
    _pendingVersion = info.version
    promptRestart(wins, info.version)
  })

  autoUpdater.on('error', (err) => {
    _state = 'idle'
    _manualCheck = false
    console.warn(`[updater] ${err.message}`)
  })

  setTimeout(() => {
    autoUpdater.checkForUpdatesAndNotify().catch(() => { /* offline */ })
  }, 10_000)
}

function promptRestart(wins: BrowserWindow[], version: string): void {
  dialog.showMessageBox(activeWin(wins), {
    type: 'info',
    title: 'Update ready — restart required',
    message: `Krevon Station ${version} is ready to install.`,
    detail: 'Restart now to apply the update, or continue and it will install on next quit.',
    buttons: ['Restart Now', 'Later'],
    defaultId: 0,
    cancelId: 1,
  }).then(({ response }) => {
    if (response === 0) autoUpdater.quitAndInstall()
  })
}

/** Call from the tray "Check for updates…" menu item. */
export function checkForUpdatesManually(wins: BrowserWindow[]): void {
  if (!app.isPackaged) {
    dialog.showMessageBox(wins[0], {
      type: 'info',
      title: 'Dev mode',
      message: 'Auto-update is disabled in development mode.',
    })
    return
  }

  const win = activeWin(wins)

  if (_state === 'ready') {
    promptRestart(wins, _pendingVersion)
    return
  }

  if (_state === 'downloading') {
    dialog.showMessageBox(win, {
      type: 'info',
      title: 'Downloading update',
      message: `Krevon Station ${_pendingVersion} is downloading…`,
      detail: 'You\'ll be prompted to restart when the download finishes.',
      buttons: ['OK'],
    })
    return
  }

  if (_state === 'checking') {
    // Piggyback on in-progress check — show result when it completes
    _manualCheck = true
    dialog.showMessageBox(win, {
      type: 'info',
      title: 'Checking for updates',
      message: 'Checking for updates…',
      detail: 'A check is already in progress.',
      buttons: ['OK'],
    })
    return
  }

  // idle — trigger fresh check
  _manualCheck = true
  autoUpdater.checkForUpdatesAndNotify().catch((err) => {
    _manualCheck = false
    _state = 'idle'
    dialog.showErrorBox('Update check failed', err?.message ?? String(err))
  })
}

export function isUpdateAvailable(): boolean {
  return _state === 'downloading' || _state === 'ready'
}
