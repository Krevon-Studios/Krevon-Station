import { BrowserWindow, screen, nativeImage } from 'electron'
import { join } from 'path'

// Fixed window size — large enough for fully expanded state.
// The pill itself shrinks/grows via CSS; we never resize the window on hover.
export const WIN_W  = 520   // wide enough for media expanded
export const WIN_H  = 200   // pill(110) + shadow(~40) + cursor margin(50)

export function createIslandWindow(): BrowserWindow {
  const { bounds } = screen.getPrimaryDisplay()

  const win = new BrowserWindow({
    width:  WIN_W,
    height: WIN_H,
    x: Math.round((bounds.width - WIN_W) / 2),
    y: 0,          // flush with top edge; pill CSS handles the 6px gap visually
    frame: false,
    transparent: true,
    backgroundColor: '#00000000',
    roundedCorners: false,
    skipTaskbar: true,
    resizable: false,
    maximizable: false,
    minimizable: false,
    focusable: false,
    hasShadow: false,
    show: false,
    icon: buildIcon(),
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false
    }
  })

  win.setAlwaysOnTop(true, 'screen-saver')
  win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: false })
  win.setIgnoreMouseEvents(true, { forward: true })

  win.once('ready-to-show', () => win.show())

  return win
}

function buildIcon(): Electron.NativeImage {
  const size = 16
  const buf = Buffer.alloc(size * size * 4)
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const i = (y * size + x) * 4
      const dist = Math.sqrt((x - 7.5) ** 2 + (y - 7.5) ** 2)
      const a = dist < 6.5 ? 220 : dist < 7.5 ? Math.round(220 * (7.5 - dist)) : 0
      buf[i] = 124; buf[i + 1] = 106; buf[i + 2] = 255; buf[i + 3] = a
    }
  }
  return nativeImage.createFromBitmap(buf, { width: size, height: size, scaleFactor: 1 })
}
