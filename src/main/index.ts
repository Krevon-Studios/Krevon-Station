import { app, ipcMain } from 'electron'
import { createIslandWindow } from './window'
import { startHookServer } from './hook-server'
import { startMediaWatcher, controlMedia } from './media-watcher'
import { startDesktopWatcher, switchVirtualDesktop } from './desktop-watcher'
import { createTray } from './tray'

app.setName('Dynamic Island')

if (!app.requestSingleInstanceLock()) {
  app.quit()
  process.exit(0)
}

app.whenReady().then(() => {
  const win = createIslandWindow()

  startHookServer(win)
  startMediaWatcher(win)
  startDesktopWatcher(win)
  createTray(win)

  // ── Hover detection (main process polling) ─────────────────────────────────
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const { screen } = require('electron') as typeof import('electron')

  let hoverActive = false
  let interactActive = false
  let hitBox = { w: 160, h: 32 } // default to IDLE_CLOSED

  ipcMain.on('set-hit-box', (_event, w: number, h: number) => {
    hitBox = { w, h }
  })

  setInterval(() => {
    const { x, y } = screen.getCursorScreenPoint()
    const b = win.getBounds()

    // The pill is horizontally centered in the window, flush with the top edge
    const cx = b.x + b.width / 2
    const overIsland =
      x >= cx - hitBox.w / 2 && x <= cx + hitBox.w / 2 &&
      y >= b.y && y <= b.y + hitBox.h

    // Top bar is always interactive for buttons
    const overTaskbar = y >= b.y && y <= b.y + 32
    const shouldInteract = overIsland || overTaskbar

    if (shouldInteract !== interactActive) {
      interactActive = shouldInteract
      win.setIgnoreMouseEvents(!shouldInteract, { forward: true })
    }

    if (overIsland !== hoverActive) {
      hoverActive = overIsland
      win.webContents.send('island:hover', overIsland)
    }
  }, 16)

  // Click-through: renderer can still fine-tune (e.g. exact pill hit-test)
  ipcMain.on('set-ignore-mouse', (_event, ignore: boolean) => {
    win.setIgnoreMouseEvents(ignore, { forward: true })
  })

  // Media controls
  ipcMain.on('control-media', (_event, action: 'play-pause' | 'next' | 'prev', sourceAppId: string) => {
    controlMedia(action, sourceAppId)
  })

  // Desktop controls
  ipcMain.on('switch-virtual-desktop', (_event, targetIndex: number) => {
    switchVirtualDesktop(targetIndex)
  })


  if (process.env['ELECTRON_RENDERER_URL']) {
    win.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    win.loadFile('out/renderer/index.html')
  }
})

app.on('window-all-closed', () => app.quit())
