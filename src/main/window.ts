import { BrowserWindow, screen, nativeImage, app } from 'electron'
import { join } from 'path'
import { existsSync } from 'fs'

export const TASKBAR_H = 32
export const DRAWER_W  = 260
export const DRAWER_MAX_H = 480

// Fixed window size — large enough for fully expanded state.
// The pill itself shrinks/grows via CSS; we never resize the window on hover.
export const WIN_W  = 520   // wide enough for media expanded
export const WIN_H  = 200   // pill(110) + shadow(~40) + cursor margin(50)

function buildBaseOptions() {
  const { bounds } = screen.getPrimaryDisplay()

  return { bounds }
}

export function createTaskbarWindow(): BrowserWindow {
  const { bounds } = buildBaseOptions()

  const win = new BrowserWindow({
    width: bounds.width,
    height: TASKBAR_H,
    x: bounds.x,
    y: bounds.y,
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

  win.setAlwaysOnTop(true, 'pop-up-menu')
  win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })

  return win
}

export function createIslandWindow(): BrowserWindow {
  const { bounds } = buildBaseOptions()

  const win = new BrowserWindow({
    width: WIN_W,
    height: WIN_H,
    x: bounds.x + Math.floor((bounds.width - WIN_W) / 2),
    y: bounds.y,   // flush with top edge; pill CSS handles the 6px gap visually
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

  win.setAlwaysOnTop(true, 'pop-up-menu')
  win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })
  win.setIgnoreMouseEvents(true, { forward: true })

  win.once('ready-to-show', () => win.show())

  return win
}

export function createDrawerWindow(): BrowserWindow {
  const { bounds } = buildBaseOptions()

  const W = 340
  const H = 480

  const win = new BrowserWindow({
    width:       W,
    height:      H,
    x:           bounds.x + bounds.width - W,
    y:           bounds.y + TASKBAR_H,
    frame:       false,
    transparent: true,
    backgroundColor: '#00000000',
    roundedCorners: false,
    skipTaskbar: true,
    resizable:   false,
    maximizable: false,
    minimizable: false,
    focusable:   true,   // true to capture blur events
    hasShadow:   false,
    show:        false,
    icon:        buildIcon(),
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,
    }
  })

  win.setAlwaysOnTop(true, 'pop-up-menu')
  win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })

  return win
}

export function createNotificationWindow(): BrowserWindow {
  const { bounds } = buildBaseOptions()
  const W = 340
  // Full height from taskbar to screen bottom — prevents bottom clipping when drawer grows
  const H = bounds.height - TASKBAR_H

  const win = new BrowserWindow({
    width:       W,
    height:      H,
    x:           bounds.x + bounds.width - W,
    y:           bounds.y + TASKBAR_H,
    frame:       false,
    transparent: true,
    backgroundColor: '#00000000',
    roundedCorners: false,
    skipTaskbar: true,
    resizable:   false,
    maximizable: false,
    minimizable: false,
    focusable:   false,
    hasShadow:   false,
    show:        false,
    icon:        buildIcon(),
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,
    }
  })

  win.setAlwaysOnTop(true, 'pop-up-menu')
  win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })
  win.setIgnoreMouseEvents(true, { forward: true })

  win.once('ready-to-show', () => win.show())

  return win
}

function buildIcon(): Electron.NativeImage {
  // Prefer the real logo bundled in resources
  const resourcesDir = app.isPackaged
    ? process.resourcesPath
    : join(app.getAppPath(), 'resources')
  const iconPath = join(resourcesDir, 'logo_solid.png')
  if (existsSync(iconPath)) {
    return nativeImage.createFromPath(iconPath)
  }

  // Fallback: generated circle (used only if logo file is missing)
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
