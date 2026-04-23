import { useState, useEffect } from 'react'
import {
  Wifi, WifiHigh, WifiLow, WifiZero, WifiOff,
  EthernetPort, GlobeOff,
  Volume2, Volume1, Volume, VolumeX,
  KeyRound,
} from 'lucide-react'

// ─────────────────────────────────────────────────────────────────────────────
// Network Icon
// ─────────────────────────────────────────────────────────────────────────────

function NetworkIcon({ network }: { network: NetworkState }) {
  const { type, signal, hasInternet } = network
  const props = { size: 14, color: 'white', strokeWidth: 1.75 }

  // No network adapter at all
  if (type === 'none') return <GlobeOff {...props} />

  // Ethernet
  if (type === 'ethernet') {
    return hasInternet ? <EthernetPort {...props} /> : <GlobeOff {...props} />
  }

  // WiFi — no internet connection (adapter present but traffic blocked)
  if (!hasInternet) return <WifiOff {...props} />

  // WiFi with internet — pick icon by signal strength
  if (signal === null || signal >= 75) return <Wifi     {...props} />
  if (signal >= 50)                    return <WifiHigh {...props} />
  if (signal >= 25)                    return <WifiLow  {...props} />
  return                                      <WifiZero {...props} />
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio Icon
// ─────────────────────────────────────────────────────────────────────────────

function AudioIcon({ audio }: { audio: AudioState }) {
  const { volume, muted } = audio
  const props = { size: 14, color: 'white', strokeWidth: 1.75 }

  if (muted || volume === 0) return <VolumeX {...props} />
  if (volume <= 33)          return <Volume  {...props} />
  if (volume <= 66)          return <Volume1 {...props} />
  return                            <Volume2 {...props} />
}

// ─────────────────────────────────────────────────────────────────────────────
// Taskbar
// ─────────────────────────────────────────────────────────────────────────────

const DEFAULT_NETWORK: NetworkState = { type: 'none', signal: null, ssid: null, hasInternet: false, vpnActive: false }
const DEFAULT_AUDIO: AudioState     = { volume: 50, muted: false }

export function Taskbar() {
  const [count, setCount]             = useState(1)
  const [activeIndex, setActiveIndex] = useState(0)
  const [network, setNetwork]         = useState<NetworkState>(DEFAULT_NETWORK)
  const [audio, setAudio]             = useState<AudioState>(DEFAULT_AUDIO)

  useEffect(() => {
    // Virtual desktops
    window.island.getVirtualDesktops().then((data) => {
      setCount(data.count)
      setActiveIndex(data.activeIndex)
    })
    const unsubDesktops = window.island.onVirtualDesktops((data) => {
      setCount(data.count)
      setActiveIndex(data.activeIndex)
    })

    // System stats — snapshot first, then live updates
    window.island.getSystemStats().then((stats) => {
      setNetwork(stats.network)
      setAudio(stats.audio)
    })
    const unsubStats = window.island.onSystemStats((stats) => {
      setNetwork(stats.network)
      setAudio(stats.audio)
    })

    return () => {
      unsubDesktops()
      unsubStats()
    }
  }, [])

  return (
    <div
      className="w-full h-[32px] bg-black flex items-center justify-between px-4 select-none relative z-10"
      style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}
    >
      {/* Left side: Pagination dots */}
      <div className="flex items-center gap-[2px] h-full cursor-pointer opacity-80 hover:opacity-100 transition-opacity pl-2">
        {Array.from({ length: count }).map((_, i) => (
          <div
            key={i}
            className="py-[6px] px-[1.5px] group cursor-pointer"
            onClick={() => window.island.switchVirtualDesktop(i)}
          >
            <div className={`h-[6px] rounded-full transition-all duration-300 bg-white ${
              i === activeIndex ? 'w-[18px]' : 'w-[6px] opacity-40 group-hover:opacity-100'
            }`} />
          </div>
        ))}
      </div>

      {/* Right side: Network + Audio (no Power icon) */}
      <button
        className="flex items-center gap-[12px] opacity-85 hover:opacity-100 transition-all duration-200 hover:bg-white/5 px-3 py-[5px] rounded-xl cursor-pointer"
        onClick={() => console.log('Open menu panel')}
      >
        {network.vpnActive && <KeyRound size={13} color="white" strokeWidth={1.75} />}
        <NetworkIcon network={network} />
        <AudioIcon   audio={audio}    />
      </button>
    </div>
  )
}
