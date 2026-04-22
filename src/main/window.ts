import { BrowserWindow, screen, nativeImage } from 'electron'
import { join } from 'path'

// 4px padding each side for clean border-radius anti-aliasing
export const PILL_PAD  = 4
export const IDLE_SIZE = { width: 165 + PILL_PAD * 2, height: 44 + PILL_PAD * 2 }

export function createIslandWindow(): BrowserWindow {
  const { bounds } = screen.getPrimaryDisplay()

  const win = new BrowserWindow({
    width: IDLE_SIZE.width,
    height: IDLE_SIZE.height,
    x: Math.round((bounds.width - IDLE_SIZE.width) / 2),
    y: 6,
    frame: false,
    transparent: true,
    backgroundColor: '#00000000',
    roundedCorners: false,   // we draw our own pill shape in CSS
    skipTaskbar: true,
    resizable: false,
    maximizable: false,
    minimizable: false,
    focusable: false,
    hasShadow: false,        // shadow drawn via CSS box-shadow
    show: false,
    icon: buildIcon(),
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false
    }
  })

  win.setAlwaysOnTop(true, 'normal')
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
