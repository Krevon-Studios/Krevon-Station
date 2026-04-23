import { app, ipcMain, screen } from 'electron'
import { createIslandWindow, createTaskbarWindow, TASKBAR_H } from './window'
import { startHookServer } from './hook-server'
import { startMediaWatcher, controlMedia } from './media-watcher'
import { startDesktopWatcher, switchVirtualDesktop } from './desktop-watcher'
import { createTray } from './tray'
import { attachAppBar } from './appbar'

app.setName('Dynamic Island')

if (!app.requestSingleInstanceLock()) {
  app.quit()
  process.exit(0)
}

app.whenReady().then(() => {
  const taskbarWin = createTaskbarWindow()
  const islandWin = createIslandWindow()
  let appBarRect = { left: 0, top: 0, right: 0, bottom: 32 }
  let taskbarShown = false
  const detachAppBar = attachAppBar(
    taskbarWin,
    (rect) => {
      appBarRect = rect
      taskbarWin.setBounds({
        x: rect.left,
        y: rect.top,
        width: rect.right - rect.left,
        height: TASKBAR_H,
      })
      if (!taskbarShown && !taskbarWin.isDestroyed()) {
        taskbarShown = true
        taskbarWin.show()
      }
      // Re-pin the island every time the app bar reports its position.
      // Windows can nudge it down to the work area start during work-area
      // settling; the retry loop catches the cases where the first attempt
      // is overridden before it sticks.
      pinIslandToTop()
    },
    (isFullscreen) => {
      if (taskbarWin.isDestroyed() || islandWin.isDestroyed()) return
      if (isFullscreen) {
        // Make windows invisible + fully non-interactive without calling hide().
        // hide()/showInactive() corrupts Electron's setIgnoreMouseEvents state
        // on Windows; setOpacity keeps the windows "alive" so nothing resets.
        taskbarWin.setOpacity(0)
        taskbarWin.setIgnoreMouseEvents(true, { forward: true })
        islandWin.setOpacity(0)
        islandWin.setIgnoreMouseEvents(true, { forward: true })
      } else {
        // Restore visibility — window state was never disrupted so the interval
        // will immediately re-evaluate the correct setIgnoreMouseEvents value.
        taskbarWin.setOpacity(1)
        islandWin.setOpacity(1)
        // -1 sentinel: never === true/false, forces interval to fire on next tick.
        interactActive = -1 as any
        hoverActive = -1 as any
        taskbarInteractActive = -1 as any
      }
    }
  )

  // Pin the island the moment it becomes visible — the work area may
  // already have changed by the time ready-to-show fires.
  islandWin.once('show', () => pinIslandToTop())

  startHookServer(islandWin)
  startMediaWatcher(islandWin)
  startDesktopWatcher(taskbarWin)
  createTray([taskbarWin, islandWin])

  // Snap the island window to y=0 (full display top, inside the reserved bar).
  // If Windows overrides the position during work-area settling, retry with a
  // short back-off until it sticks (capped at 5 attempts = 250 ms worst-case).
  function pinIslandToTop(retriesLeft = 5) {
    if (islandWin.isDestroyed()) return
    const { bounds } = screen.getPrimaryDisplay()
    islandWin.setBounds({
      x: bounds.x + Math.floor((bounds.width - islandWin.getBounds().width) / 2),
      y: bounds.y,
      width: islandWin.getBounds().width,
      height: islandWin.getBounds().height,
    })
    if (islandWin.getBounds().y !== bounds.y && retriesLeft > 0) {
      setTimeout(() => pinIslandToTop(retriesLeft - 1), 50)
    }
  }

  let hoverActive = false
  let interactActive = false
  let taskbarInteractActive = true
  let hitBox = { w: 160, h: 32 } // default to IDLE_CLOSED

  ipcMain.on('set-hit-box', (_event, w: number, h: number) => {
    hitBox = { w, h }
  })

  setInterval(() => {
    const { x, y } = screen.getCursorScreenPoint()
    const bounds = islandWin.getBounds()

    // The pill is horizontally centered in the window, flush with the top edge.
    const centerX = bounds.x + bounds.width / 2
    const overIsland =
      x >= centerX - hitBox.w / 2 && x <= centerX + hitBox.w / 2 &&
      y >= bounds.y && y <= bounds.y + hitBox.h

    if (overIsland !== interactActive) {
      interactActive = overIsland
      islandWin.setIgnoreMouseEvents(!overIsland, { forward: true })
    }

    if (overIsland !== hoverActive) {
      hoverActive = overIsland
      islandWin.webContents.send('island:hover', overIsland)
    }

    // Ensure taskbar only captures clicks in its exact visual area (top 32px)
    if (!taskbarWin.isDestroyed()) {
      const tbBounds = taskbarWin.getBounds()
      const overTaskbar = y >= tbBounds.y && y <= tbBounds.y + TASKBAR_H
      if (overTaskbar !== taskbarInteractActive) {
        taskbarInteractActive = overTaskbar
        taskbarWin.setIgnoreMouseEvents(!overTaskbar, { forward: true })
      }
    }
  }, 16)

  ipcMain.on('set-ignore-mouse', (_event, ignore: boolean) => {
    islandWin.setIgnoreMouseEvents(ignore, { forward: true })
  })

  ipcMain.on('control-media', (_event, action: 'play-pause' | 'next' | 'prev', sourceAppId: string) => {
    controlMedia(action, sourceAppId)
  })

  ipcMain.on('switch-virtual-desktop', (_event, targetIndex: number) => {
    switchVirtualDesktop(targetIndex)
  })

  const syncTaskbarWindow = () => {
    const width = appBarRect.right - appBarRect.left
    taskbarWin.setBounds({
      x: appBarRect.left,
      y: appBarRect.top,
      width: width > 0 ? width : taskbarWin.getBounds().width,
      height: TASKBAR_H,
    })
  }

  const syncIslandWindow = () => {
    const { bounds } = screen.getPrimaryDisplay()
    islandWin.setBounds({
      x: bounds.x + Math.floor((bounds.width - islandWin.getBounds().width) / 2),
      y: bounds.y,
      width: islandWin.getBounds().width,
      height: islandWin.getBounds().height,
    })
  }

  const handleDisplayChange = () => {
    syncTaskbarWindow()
    syncIslandWindow()
  }
  screen.on('display-metrics-changed', handleDisplayChange)
  screen.on('display-added', handleDisplayChange)
  screen.on('display-removed', handleDisplayChange)

  if (process.env['ELECTRON_RENDERER_URL']) {
    taskbarWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=taskbar`)
    islandWin.loadURL(`${process.env['ELECTRON_RENDERER_URL']}?view=island`)
  } else {
    taskbarWin.loadFile('out/renderer/index.html', { search: 'view=taskbar' })
    islandWin.loadFile('out/renderer/index.html', { search: 'view=island' })
  }

  let cleanedUp = false
  const cleanup = () => {
    if (cleanedUp) return
    cleanedUp = true
    screen.off('display-metrics-changed', handleDisplayChange)
    screen.off('display-added', handleDisplayChange)
    screen.off('display-removed', handleDisplayChange)
    detachAppBar()
  }

  app.on('before-quit', cleanup)
  app.on('will-quit', cleanup)
  taskbarWin.on('closed', cleanup)
  islandWin.on('closed', cleanup)

  process.once('SIGINT', () => {
    cleanup()
    process.exit(0)
  })
  process.once('SIGTERM', () => {
    cleanup()
    process.exit(0)
  })
  process.once('uncaughtException', (error) => {
    console.error(error)
    cleanup()
    process.exit(1)
  })
  process.once('unhandledRejection', (reason) => {
    console.error(reason)
    cleanup()
    process.exit(1)
  })
})

app.on('window-all-closed', () => app.quit())
