import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('island', {
  onSessionStart: (cb: (data: unknown) => void) =>
    ipcRenderer.on('island:session-start', (_e, d) => cb(d)),

  onToolActive: (cb: (data: unknown) => void) =>
    ipcRenderer.on('island:tool-active', (_e, d) => cb(d)),

  onTaskDone: (cb: (data: unknown) => void) =>
    ipcRenderer.on('island:task-done', (_e, d) => cb(d)),

  onMedia: (cb: (data: unknown) => void) =>
    ipcRenderer.on('island:media', (_e, d) => cb(d)),

  // Hover state driven by main-process polling (screen.getCursorScreenPoint)
  onHover: (cb: (over: boolean) => void) =>
    ipcRenderer.on('island:hover', (_e, over) => cb(over as boolean)),

  controlMedia: (action: 'play-pause' | 'next' | 'prev', sourceAppId: string) =>
    ipcRenderer.send('control-media', action, sourceAppId),

  removeAllListeners: () => {
    ipcRenderer.removeAllListeners('island:session-start')
    ipcRenderer.removeAllListeners('island:tool-active')
    ipcRenderer.removeAllListeners('island:task-done')
    ipcRenderer.removeAllListeners('island:media')
    ipcRenderer.removeAllListeners('island:hover')
  },

  setIgnoreMouse: (ignore: boolean) => ipcRenderer.send('set-ignore-mouse', ignore),
  setWindowSize: (_w: number, _h: number) => {},
  setHitBox: (w: number, h: number) => ipcRenderer.send('set-hit-box', w, h)
})
