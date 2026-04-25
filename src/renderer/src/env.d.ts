type NetworkType = 'wifi' | 'none'

interface NetworkState {
  type:        NetworkType
  signal:      number | null
  hasInternet: boolean
  ssid:        string | null
  vpnActive:   boolean
}

interface AudioState {
  volume: number
  muted:  boolean
}

interface SystemStats {
  network: NetworkState
  audio:   AudioState
}

interface WifiNetwork {
  ssid:      string
  signal:    number
  secured:   boolean
  connected: boolean
}

interface AudioDevice {
  id:   string
  name: string
}

interface AudioSession {
  pid:    number
  name:   string
  volume: number
  muted:  boolean
}

interface IslandAPI {
  // Island events
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
  setHitBox:          (w: number, h: number) => void
  getSystemStats:     () => Promise<SystemStats>
  onSystemStats:      (cb: (data: SystemStats) => void) => () => void

  // Drawer control
  openDrawer:         (type: string) => void
  closeDrawer:        () => void
  requestCloseDrawer: () => void
  onDrawerShow:       (cb: (type: string) => void) => () => void
  onDrawerClosed:     (cb: () => void) => () => void
  onDrawerForceClose: (cb: () => void) => () => void
  onDrawerHeight:     (cb: (h: number) => void) => () => void

  // Audio control
  setSystemVolume:  (volume: number)                                => Promise<void>
  setSystemMute:    (mute: boolean)                                 => Promise<void>
  setAppVolume:     (pid: number, vol: number)                      => Promise<void>
  getAudioDevices:  () => Promise<{ devices: AudioDevice[]; activeId: string }>
  getAudioSessions: () => Promise<AudioSession[]>
  setAudioDevice:   (deviceId: string)                              => Promise<void>
  setSessionVolume: (pid: number, volume?: number, muted?: boolean) => Promise<void>
  getAppIcon:       (pid: number)                                    => Promise<string | null>
  getNotifIcon:     (appId: string)                                  => Promise<string | null>

  // WiFi control
  scanWifiNetworks: (force?: boolean)   => Promise<WifiNetwork[]>
  setWifiEnabled:   (enable: boolean)   => Promise<void>
  connectWifi:      (ssid: string)      => Promise<void>
  getWifiState:     ()                  => Promise<{ enabled: boolean }>

  // System actions
  getUserInfo:     ()               => Promise<{ avatar: string | null; name: string }>
  systemAction:    (action: string) => Promise<void>

  // Drawer sizing
  setDrawerHeight: (h: number)      => void

  // Notifications
  onNotifications:      (cb: (data: unknown) => void) => () => void
  dismissNotifications: (ids: number[]) => Promise<void>

  // Accent color
  getAccentColor: () => Promise<{ r: number; g: number; b: number }>
  onAccentColor:  (cb: (data: { r: number; g: number; b: number }) => void) => () => void
}

declare interface Window {
  island: IslandAPI
}
