import { app, ipcMain, screen } from 'electron'
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

  // Click-through toggle — renderer sends when hovering pill content
  ipcMain.on('set-ignore-mouse', (_event, ignore: boolean) => {
    win.setIgnoreMouseEvents(ignore, { forward: true })
  })

  // Media controls — renderer sends action + target session AUMID
  ipcMain.on('control-media', (_event, action: 'play-pause' | 'next' | 'prev', sourceAppId: string) => {
    controlMedia(action, sourceAppId)
  })

  // Dynamic resize: window = pill size, renderer drives it on state change
  ipcMain.on('set-window-size', (_event, w: number, h: number) => {
    const { bounds } = screen.getPrimaryDisplay()
    win.setBounds({
      x: Math.round((bounds.width - w) / 2),
      y: 6,
      width: w,
      height: h
    })
  })

  if (process.env['ELECTRON_RENDERER_URL']) {
    win.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    win.loadFile('out/renderer/index.html')
  }
})

app.on('window-all-closed', () => app.quit())
