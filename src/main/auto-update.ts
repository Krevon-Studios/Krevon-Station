/**
 * auto-update.ts
 *
 * Wires up electron-updater for GitHub Releases-based auto-update.
 * Only active in packaged builds — no-ops in dev mode.
 *
 * Update flow:
 *   1. On app ready → silently check for updates in background
 *   2. Update found → download automatically
 *   3. Download complete → notify via tray / dialog; install on next restart
 *   4. User can also trigger a manual check from the tray menu
 */

import { app, dialog, BrowserWindow } from 'electron'
import { autoUpdater } from 'electron-updater'

let _updateAvailable = false

export function initAutoUpdater(wins: BrowserWindow[]): void {
  if (!app.isPackaged) return   // skip entirely in dev

  // Silent background logging — writes to %AppData%\Krevon Station\logs\
  autoUpdater.logger = null
  autoUpdater.autoDownload = true
  autoUpdater.autoInstallOnAppQuit = true

  autoUpdater.on('update-available', (info) => {
    _updateAvailable = true
    console.log(`[updater] update available: ${info.version}`)
  })

  autoUpdater.on('update-downloaded', (info) => {
    // Offer to restart immediately; user can defer until next quit
    const activeWin = wins.find(w => !w.isDestroyed() && w.isVisible())
    const buttons = ['Restart Now', 'Later']
    dialog.showMessageBox(activeWin ?? wins[0], {
      type: 'info',
      title: 'Update ready',
      message: `Krevon Station ${info.version} is ready to install.`,
      detail: 'The update will be applied when you restart. Restart now?',
      buttons,
      defaultId: 0,
      cancelId: 1,
    }).then(({ response }) => {
      if (response === 0) autoUpdater.quitAndInstall()
    })
  })

  autoUpdater.on('error', (err) => {
    console.warn(`[updater] ${err.message}`)
  })

  // Initial check — delay 10 s to avoid blocking app startup
  setTimeout(() => {
    autoUpdater.checkForUpdatesAndNotify().catch(() => { /* offline */ })
  }, 10_000)
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
  autoUpdater.checkForUpdatesAndNotify().catch((err) => {
    dialog.showErrorBox('Update check failed', err?.message ?? String(err))
  })
}

export function isUpdateAvailable(): boolean {
  return _updateAvailable
}
