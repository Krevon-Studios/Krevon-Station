import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('island', {
  onSessionStart: (cb: (data: unknown) => void) => {
    const fn = (_e: any, d: unknown) => cb(d)
    ipcRenderer.on('island:session-start', fn)
    return () => ipcRenderer.removeListener('island:session-start', fn)
  },

  onToolActive: (cb: (data: unknown) => void) => {
    const fn = (_e: any, d: unknown) => cb(d)
    ipcRenderer.on('island:tool-active', fn)
    return () => ipcRenderer.removeListener('island:tool-active', fn)
  },

  onTaskDone: (cb: (data: unknown) => void) => {
    const fn = (_e: any, d: unknown) => cb(d)
    ipcRenderer.on('island:task-done', fn)
    return () => ipcRenderer.removeListener('island:task-done', fn)
  },

  onMedia: (cb: (data: unknown) => void) => {
    const fn = (_e: any, d: unknown) => cb(d)
    ipcRenderer.on('island:media', fn)
    return () => ipcRenderer.removeListener('island:media', fn)
  },

  onHover: (cb: (over: boolean) => void) => {
    const fn = (_e: any, over: unknown) => cb(over as boolean)
    ipcRenderer.on('island:hover', fn)
    return () => ipcRenderer.removeListener('island:hover', fn)
  },

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
