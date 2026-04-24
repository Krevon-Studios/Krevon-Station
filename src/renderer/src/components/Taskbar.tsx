import { useState, useEffect, useRef } from 'react'
import {
  Wifi, WifiHigh, WifiLow, WifiZero, GlobeOff,
  Volume2, Volume1, Volume, VolumeX,
  KeyRound,
} from 'lucide-react'

function NetworkIcon({ network }: { network: NetworkState }) {
  const { type, signal, hasInternet } = network
  const p = { size: 14, color: 'white', strokeWidth: 1.75 }
  if (type === 'none' || !hasInternet) return <GlobeOff {...p} />
  if (signal === null || signal >= 75) return <Wifi     {...p} />
  if (signal >= 50)                    return <WifiHigh {...p} />
  if (signal >= 25)                    return <WifiLow  {...p} />
  return                                      <WifiZero {...p} />
}

function AudioIcon({ audio }: { audio: AudioState }) {
  const { volume, muted } = audio
  const p = { size: 14, color: 'white', strokeWidth: 1.75 }
  if (muted || volume === 0) return <VolumeX {...p} />
  if (volume <= 33)          return <Volume  {...p} />
  if (volume <= 66)          return <Volume1 {...p} />
  return                            <Volume2 {...p} />
}

const DEFAULT_NETWORK: NetworkState = { type: 'none', signal: null, ssid: null, hasInternet: false, vpnActive: false }
const DEFAULT_AUDIO: AudioState     = { volume: 50, muted: false }

export function Taskbar() {
  const [count,        setCount]        = useState(1)
  const [activeIndex,  setActiveIndex]  = useState(0)
  const [network,      setNetwork]      = useState<NetworkState>(DEFAULT_NETWORK)
  const [audio,        setAudio]        = useState<AudioState>(DEFAULT_AUDIO)
  const [drawerOpen,   setDrawerOpen]   = useState(false)

  // Track last time the drawer was externally closed to debounce re-open
  const lastClosedAt = useRef(0)

  useEffect(() => {
    window.island.getVirtualDesktops().then(d => { setCount(d.count); setActiveIndex(d.activeIndex) })
    const unsubDesktops = window.island.onVirtualDesktops(d => { setCount(d.count); setActiveIndex(d.activeIndex) })

    window.island.getSystemStats().then(s => { setNetwork(s.network); setAudio(s.audio) })
    const unsubStats = window.island.onSystemStats(s => { setNetwork(s.network); setAudio(s.audio) })

    // Listen for drawer closing (from overlay click, Escape, etc.)
    const unsubClose = window.island.onDrawerClosed(() => {
      lastClosedAt.current = Date.now()
      setDrawerOpen(false)
    })

    return () => { unsubDesktops(); unsubStats(); unsubClose() }
  }, [])

  const toggleDrawer = () => {
    // Debounce: if the drawer just closed (within 200ms) don't reopen
    if (drawerOpen || Date.now() - lastClosedAt.current < 200) {
      window.island.requestCloseDrawer()
      setDrawerOpen(false)
    } else {
      window.island.openDrawer('combined')
      setDrawerOpen(true)
    }
  }

  return (
    <div
      className="w-full h-[32px] bg-black flex items-center justify-between px-4 select-none"
      style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}
    >
      {/* Left: pagination dots */}
      <div className="flex items-center gap-[2px] h-full pl-2">
        {Array.from({ length: count }).map((_, i) => (
          <div key={i} className="py-[6px] px-[1.5px] group cursor-pointer" onClick={() => window.island.switchVirtualDesktop(i)}>
            <div className={`h-[6px] rounded-full transition-all duration-300 bg-white ${i === activeIndex ? 'w-[18px]' : 'w-[6px] opacity-40 group-hover:opacity-100'}`} />
          </div>
        ))}
      </div>

      {/* Right: combined WiFi + Sound button */}
      <button
        id="taskbar-control-btn"
        onClick={toggleDrawer}
        aria-label="System controls"
        aria-expanded={drawerOpen}
        className={`flex items-center gap-[10px] px-[10px] h-[24px] rounded-[8px] transition-all duration-150 cursor-pointer ${
          drawerOpen ? 'bg-white/14 opacity-100' : 'opacity-80 hover:opacity-100 hover:bg-white/6'
        }`}
      >
        {network.vpnActive && <KeyRound size={13} color="white" strokeWidth={1.75} />}
        <NetworkIcon network={network} />
        <AudioIcon audio={audio} />
      </button>
    </div>
  )
}
