interface IslandAPI {
  onSessionStart:     (cb: (data: unknown) => void) => () => void
  onToolActive:       (cb: (data: unknown) => void) => () => void
  onTaskDone:         (cb: (data: unknown) => void) => () => void
  onMedia:            (cb: (data: unknown) => void) => () => void
  getVirtualDesktops: () => Promise<{ count: number; activeIndex: number }>
  onVirtualDesktops:  (cb: (data: { count: number, activeIndex: number }) => void) => () => void
  onHover:            (cb: (over: boolean) => void) => () => void
  switchVirtualDesktop: (targetIndex: number) => void
  controlMedia:       (action: 'play-pause' | 'next' | 'prev', sourceAppId: string) => void
  removeAllListeners: () => void
  setIgnoreMouse:     (ignore: boolean) => void
  setWindowSize:      (w: number, h: number) => void
  setHitBox:          (w: number, h: number) => void
}

declare interface Window {
  island: IslandAPI
}
