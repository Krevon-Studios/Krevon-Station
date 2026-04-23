import { app, ipcMain } from 'electron'
import { createIslandWindow } from './window'
import { startHookServer } from './hook-server'
import { startMediaWatcher, controlMedia } from './media-watcher'
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
  createTray(win)

  // ── Hover detection (main process polling) ─────────────────────────────────
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const { screen } = require('electron') as typeof import('electron')

  let hoverActive = false
  let hitBox = { w: 160, h: 32 } // default to IDLE_CLOSED

  ipcMain.on('set-hit-box', (_event, w: number, h: number) => {
    hitBox = { w, h }
  })

  setInterval(() => {
    const { x, y } = screen.getCursorScreenPoint()
    const b = win.getBounds()

    // The pill is horizontally centered in the window, flush with the top edge
    const cx = b.x + b.width / 2
    const over =
      x >= cx - hitBox.w / 2 && x <= cx + hitBox.w / 2 &&
      y >= b.y && y <= b.y + hitBox.h

    if (over === hoverActive) return

    hoverActive = over
    win.setIgnoreMouseEvents(!over, { forward: true })
    win.webContents.send('island:hover', over)
  }, 16)

  // Click-through: renderer can still fine-tune (e.g. exact pill hit-test)
  ipcMain.on('set-ignore-mouse', (_event, ignore: boolean) => {
    win.setIgnoreMouseEvents(ignore, { forward: true })
  })

  // Media controls
  ipcMain.on('control-media', (_event, action: 'play-pause' | 'next' | 'prev', sourceAppId: string) => {
    controlMedia(action, sourceAppId)
  })


  if (process.env['ELECTRON_RENDERER_URL']) {
    win.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    win.loadFile('out/renderer/index.html')
  }
})

app.on('window-all-closed', () => app.quit())
