import { Tray, Menu, nativeImage, app } from 'electron'
import type { BrowserWindow } from 'electron'

export function createTray(win: BrowserWindow): void {
  const icon = buildTrayIcon()
  const tray = new Tray(icon)
  tray.setToolTip('Dynamic Island')

  const menu = Menu.buildFromTemplate([
    {
      label: 'Show / Hide',
      click: () => {
        if (win.isVisible()) {
          win.hide()
        } else {
          win.show()
        }
      }
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
