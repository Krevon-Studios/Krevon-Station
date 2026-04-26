import { Tray, Menu, nativeImage, app } from 'electron'
import { join } from 'path'
import { existsSync } from 'fs'
import { checkForUpdatesManually } from './auto-update'
import type { BrowserWindow } from 'electron'

export function createTray(wins: BrowserWindow[]): void {
  const icon = buildTrayIcon()
  const tray = new Tray(icon)
  tray.setToolTip('Krevon Station')

  const menu = Menu.buildFromTemplate([
    {
      label: 'Show / Hide',
      click: () => {
        const anyVisible = wins.some((win) => win.isVisible())
        if (anyVisible) {
          wins.forEach((win) => win.hide())
        } else {
          wins.forEach((win) => win.show())
        }
      }
    },
    { type: 'separator' },
    {
      label: 'Check for updates…',
      click: () => checkForUpdatesManually(wins)
    },
    { type: 'separator' },
    {
      label: 'Quit',
      click: () => app.quit()
    }
  ])

  tray.setContextMenu(menu)
}

function buildTrayIcon(): Electron.NativeImage {
  // Prefer the real logo from resources (packaged or dev)
  const resourcesDir = app.isPackaged
    ? process.resourcesPath
    : join(app.getAppPath(), 'resources')
  const iconPath = join(resourcesDir, 'logo_transparent.png')
  if (existsSync(iconPath)) {
    return nativeImage.createFromPath(iconPath)
  }

  // Fallback: generated circle (used only if logo file is missing)
  const size = 16
  const buf = Buffer.alloc(size * size * 4)
  const cx = 7.5
  const cy = 7.5
  const r = 6.5

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const i = (y * size + x) * 4
      const dist = Math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
      const alpha = dist < r ? 220 : dist < r + 1 ? Math.round(220 * (r + 1 - dist)) : 0
      buf[i] = 124
      buf[i + 1] = 106
      buf[i + 2] = 255
      buf[i + 3] = alpha
    }
  }

  return nativeImage.createFromBitmap(buf, { width: size, height: size, scaleFactor: 1 })
}
