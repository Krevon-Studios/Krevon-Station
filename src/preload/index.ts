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

  getVirtualDesktops: (): Promise<{ count: number; activeIndex: number }> =>
    ipcRenderer.invoke('get-virtual-desktops'),

  onVirtualDesktops: (cb: (data: { count: number, activeIndex: number }) => void) => {
    const fn = (_e: any, d: unknown) => cb(d as { count: number, activeIndex: number })
    ipcRenderer.on('island:virtual-desktops', fn)
    return () => ipcRenderer.removeListener('island:virtual-desktops', fn)
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
    ipcRenderer.removeAllListeners('island:virtual-desktops')
    ipcRenderer.removeAllListeners('island:hover')
    ipcRenderer.removeAllListeners('system-stats')
    ipcRenderer.removeAllListeners('drawer:show')
    ipcRenderer.removeAllListeners('drawer:closed')
    ipcRenderer.removeAllListeners('accent-color')
  },

  switchVirtualDesktop: (targetIndex: number) => ipcRenderer.send('switch-virtual-desktop', targetIndex),

  getSystemStats: () => ipcRenderer.invoke('get-system-stats'),

  onSystemStats: (cb: (data: unknown) => void) => {
    const fn = (_e: any, d: unknown) => cb(d)
    ipcRenderer.on('system-stats', fn)
    return () => ipcRenderer.removeListener('system-stats', fn)
  },

  setIgnoreMouse: (ignore: boolean) => ipcRenderer.send('set-ignore-mouse', ignore),
  setWindowSize: (_w: number, _h: number) => {},
  setHitBox: (w: number, h: number) => ipcRenderer.send('set-hit-box', w, h),

  // ── Drawer control ─────────────────────────────────────────────────────────
  openDrawer:          (type: string) => ipcRenderer.send('drawer:open', type),
  closeDrawer:         ()             => ipcRenderer.send('drawer:close'),
  requestCloseDrawer:  ()             => ipcRenderer.send('drawer:request-close'),

  onDrawerShow: (cb: (type: string) => void) => {
    const fn = (_e: any, type: string) => cb(type)
    ipcRenderer.on('drawer:show', fn)
    return () => ipcRenderer.removeListener('drawer:show', fn)
  },

  onDrawerClosed: (cb: () => void) => {
    const fn = () => cb()
    ipcRenderer.on('drawer:closed', fn)
    return () => ipcRenderer.removeListener('drawer:closed', fn)
  },

  onDrawerForceClose: (cb: () => void) => {
    const fn = () => cb()
    ipcRenderer.on('drawer:force-close', fn)
    return () => ipcRenderer.removeListener('drawer:force-close', fn)
  },

  // ── Audio control ──────────────────────────────────────────────────────────
  setSystemVolume:  (volume: number)                                    => ipcRenderer.invoke('set-system-volume', volume),
  setSystemMute:    (mute: boolean)                                     => ipcRenderer.invoke('set-system-mute', mute),
  setAppVolume:     (pid: number, vol: number)                          => ipcRenderer.invoke('set-app-volume', pid, vol),
  getAudioDevices:  ()                                                  => ipcRenderer.invoke('get-audio-devices'),
  getAudioSessions: ()                                                  => ipcRenderer.invoke('get-audio-sessions'),
  setAudioDevice:   (deviceId: string)                                  => ipcRenderer.invoke('set-audio-device', deviceId),
  setSessionVolume: (pid: number, volume?: number, muted?: boolean)     => ipcRenderer.invoke('set-session-volume', pid, volume, muted),
  getAppIcon:       (pid: number)                                       => ipcRenderer.invoke('get-app-icon', pid),

  // ── WiFi control ───────────────────────────────────────────────────────────
  scanWifiNetworks: ()                   => ipcRenderer.invoke('scan-wifi-networks'),
  setWifiEnabled:   (enable: boolean)    => ipcRenderer.invoke('set-wifi-enabled', enable),
  connectWifi:      (ssid: string)       => ipcRenderer.invoke('connect-wifi', ssid),
  getWifiState:     ()                   => ipcRenderer.invoke('get-wifi-state'),

  // ── System actions ─────────────────────────────────────────────────────────
  getUserInfo:     (): Promise<{ avatar: string | null; name: string }> => ipcRenderer.invoke('get-user-info'),
  systemAction:    (action: string): Promise<void> => ipcRenderer.invoke('system-action', action),

  // ── Drawer sizing ──────────────────────────────────────────────────────────
  setDrawerHeight: (h: number): void             => ipcRenderer.send('drawer:resize', h),

  // ── Accent color ───────────────────────────────────────────────────────────
  getAccentColor: (): Promise<{ r: number; g: number; b: number }> =>
    ipcRenderer.invoke('get-accent-color'),

  onAccentColor: (cb: (data: { r: number; g: number; b: number }) => void) => {
    const fn = (_e: any, d: { r: number; g: number; b: number }) => cb(d)
    ipcRenderer.on('accent-color', fn)
    return () => ipcRenderer.removeListener('accent-color', fn)
  },
})
