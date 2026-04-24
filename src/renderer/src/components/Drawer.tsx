/**
 * Drawer.tsx — Windows-style dark control panel
 *
 * Three pages (framer-motion fade):
 *   main  → volume slider + WiFi pill
 *   sound → output devices + per-app volume mixer
 *   wifi  → full network list with toggle
 *
 * State resets to "main" on every open.
 * All audio data comes from Python event-driven monitors (zero polling).
 */

import { useState, useEffect, useRef, useCallback } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import {
  Wifi, WifiHigh, WifiLow, WifiZero, GlobeOff,
  KeyRound, Volume2, Volume1, Volume, VolumeX, Check, RefreshCw,
  ChevronRight, ChevronLeft, Speaker, Headphones, MonitorSpeaker,
  SlidersHorizontal,
} from 'lucide-react'

// ── Motion presets ────────────────────────────────────────────────────────────

const SI = { type: 'spring', stiffness: 340, damping: 30, mass: 0.65 } as const

const CARD_ENTER = { opacity: 1, y: 0, transition: { type: 'spring', stiffness: 380, damping: 28, mass: 0.7 } }
const CARD_EXIT = { opacity: 0, y: -8, transition: { duration: 0.15, ease: 'easeIn' } }

const PAGE_IN = { opacity: 0, y: 6 }
const PAGE_MID = { opacity: 1, y: 0, transition: { duration: 0.16, ease: [0.25, 0.1, 0.25, 1] } }
const PAGE_OUT = { opacity: 0, y: -4, transition: { duration: 0.1, ease: 'easeIn' } }

// ── Icon helpers ──────────────────────────────────────────────────────────────

function NetIcon({ n, size = 16, color = 'white' }: { n: NetworkState; size?: number; color?: string }) {
  const p = { size, strokeWidth: 1.75, color }
  if (n.type === 'none') return <GlobeOff {...p} />
  const sig = n.signal
  
  const getIcon = () => {
    if (sig === null || sig >= 75) return <Wifi     {...p} />
    if (sig >= 50) return <WifiHigh {...p} />
    if (sig >= 25) return <WifiLow  {...p} />
    return <WifiZero {...p} />
  }

  return (
    <div className="relative flex items-center justify-center" style={{ width: size, height: size }}>
      <div className="absolute inset-0 opacity-25">
        <Wifi {...p} />
      </div>
      <div className="absolute inset-0">
        {getIcon()}
      </div>
    </div>
  )
}

function SigIcon({ signal, size = 14, color = 'white' }: { signal: number; size?: number; color?: string }) {
  const p = { size, strokeWidth: 1.75, color }
  
  const getIcon = () => {
    if (signal >= 75) return <Wifi     {...p} />
    if (signal >= 50) return <WifiHigh {...p} />
    if (signal >= 25) return <WifiLow  {...p} />
    return <WifiZero {...p} />
  }

  return (
    <div className="relative flex items-center justify-center" style={{ width: size, height: size }}>
      <div className="absolute inset-0 opacity-25">
        <Wifi {...p} />
      </div>
      <div className="absolute inset-0">
        {getIcon()}
      </div>
    </div>
  )
}

function VolIcon({ volume, muted, size = 15, color = 'white' }: {
  volume: number; muted: boolean; size?: number; color?: string
}) {
  const p = { size, strokeWidth: 1.75, color }
  if (muted || volume === 0) return <VolumeX {...p} />
  if (volume <= 33) return <Volume  {...p} />
  if (volume <= 66) return <Volume1 {...p} />
  return <Volume2 {...p} />
}

function DevIcon({ name, size = 14, color = 'white' }: { name: string; size?: number; color?: string }) {
  const n = name.toLowerCase()
  const p = { size, strokeWidth: 1.75, color }
  if (n.includes('headphone') || n.includes('headset') || n.includes('earphone')) return <Headphones    {...p} />
  if (n.includes('monitor') || n.includes('display') || n.includes('hdmi')) return <MonitorSpeaker {...p} />
  return <Speaker {...p} />
}

// ── Toggle switch ─────────────────────────────────────────────────────────────

function Toggle({ on, onToggle, disabled }: { on: boolean; onToggle(): void; disabled?: boolean }) {
  return (
    <button
      onClick={onToggle}
      disabled={disabled}
      className={`relative w-[40px] h-[22px] rounded-full transition-colors duration-250 cursor-pointer shrink-0
        ${on ? 'bg-[#3b82f6]' : 'bg-white/15'} ${disabled ? 'opacity-50 cursor-not-allowed' : ''}`}
    >
      <motion.span
        className="absolute top-[3px] w-[16px] h-[16px] rounded-full bg-white shadow-sm"
        animate={{ left: on ? '21px' : '3px' }}
        transition={{ type: 'spring', stiffness: 500, damping: 38 }}
      />
    </button>
  )
}

// ── Drag slider ───────────────────────────────────────────────────────────────

function Slider({ value, onChange, onCommit }: {
  value: number
  onChange(v: number): void
  onCommit(v: number): void
}) {
  const ref = useRef<HTMLDivElement>(null)
  const dragging = useRef(false)

  const clamp = (cx: number) => {
    const r = ref.current!.getBoundingClientRect()
    return Math.max(0, Math.min(100, Math.round(((cx - r.left) / r.width) * 100)))
  }

  return (
    <div
      ref={ref}
      className="relative h-[22px] flex items-center cursor-pointer group select-none"
      onPointerDown={e => { dragging.current = true; ref.current!.setPointerCapture(e.pointerId); onChange(clamp(e.clientX)) }}
      onPointerMove={e => { if (dragging.current) onChange(clamp(e.clientX)) }}
      onPointerUp={e => { if (!dragging.current) return; dragging.current = false; const v = clamp(e.clientX); onChange(v); onCommit(v) }}
    >
      <div className="w-full h-[5px] rounded-full bg-white/15">
        <div className="h-full rounded-full bg-[#3b82f6]/90" style={{ width: `${value}%` }} />
      </div>
      <div
        className="absolute w-[15px] h-[15px] rounded-full bg-white shadow-md group-hover:scale-110 transition-transform duration-75"
        style={{ left: `calc(${value}% - 7.5px)` }}
      />
    </div>
  )
}

// ── Sub-page header ───────────────────────────────────────────────────────────

function PageHeader({ title, onBack, right }: {
  title: string
  onBack(): void
  right?: React.ReactNode
}) {
  return (
    <div className="flex items-center gap-[8px] px-[14px] py-[11px] border-b border-white/[0.06]">
      <button
        onClick={onBack}
        className="w-[26px] h-[26px] rounded-full hover:bg-white/10 active:bg-white/5 flex items-center justify-center transition-colors cursor-pointer shrink-0"
      >
        <ChevronLeft size={14} color="rgba(255,255,255,0.65)" strokeWidth={2} />
      </button>
      <span className="flex-1 text-[13px] font-semibold text-white leading-none">{title}</span>
      {right}
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────────────────
// PAGE: Main
// ─────────────────────────────────────────────────────────────────────────────

function MainPage({
  volume, muted, network, wifiEnabled, wifiToggling,
  onVolumeChange, onVolumeCommit, onMuteToggle,
  onWifiToggle, onGoSound, onGoWifi,
}: {
  volume: number
  muted: boolean
  network: NetworkState
  wifiEnabled: boolean
  wifiToggling: boolean
  onVolumeChange(v: number): void
  onVolumeCommit(v: number): void
  onMuteToggle(): void
  onWifiToggle(): void
  onGoSound(): void
  onGoWifi(): void
}) {
  const displayVol = muted ? 0 : volume
  const isActive = wifiEnabled && network.type === 'wifi'
  const connLabel = network.ssid ?? (wifiEnabled ? 'Not connected' : 'Off')

  return (
    <div className="flex flex-col gap-[10px] p-[14px]">

      {/* ── WiFi pill ────────────────────────────────────────────────────── */}
      <div className={`flex rounded-[14px] overflow-hidden transition-colors duration-200
        ${isActive ? 'bg-[#3b82f6]/15' : 'bg-white/7'}`}
      >
        <button
          onClick={onWifiToggle}
          disabled={wifiToggling}
          className="flex-1 flex items-center gap-[10px] px-[12px] py-[10px] cursor-pointer text-left
            hover:brightness-110 active:brightness-90 transition-all disabled:opacity-60"
        >
          <div className={`w-[32px] h-[32px] rounded-full flex items-center justify-center shrink-0 transition-colors
            ${isActive ? 'bg-[#3b82f6]/28' : 'bg-white/10'}`}
          >
            <NetIcon n={network} size={15} color={isActive ? '#93c5fd' : 'rgba(255,255,255,0.65)'} />
          </div>
          <div className="flex flex-col gap-[3px]">
            <span className="text-[13px] font-semibold text-white leading-none">Wi-Fi</span>
            <span className="text-[11px] text-white/45 leading-none truncate max-w-[150px]">{connLabel}</span>
          </div>
        </button>

        <div className="w-[1px] bg-white/[0.06] my-[9px]" />

        <button
          onClick={onGoWifi}
          className="px-[13px] flex items-center justify-center hover:bg-white/8 active:bg-white/4 transition-colors cursor-pointer"
          aria-label="Wi-Fi settings"
        >
          <ChevronRight size={14} color="rgba(255,255,255,0.5)" strokeWidth={2} />
        </button>
      </div>

      {/* ── Volume row ───────────────────────────────────────────────────── */}
      <div className="flex items-center gap-[8px]">
        <button
          onClick={onMuteToggle}
          className="w-[32px] h-[32px] rounded-full bg-white/7 hover:bg-white/13 active:bg-white/5
            flex items-center justify-center shrink-0 transition-colors cursor-pointer"
        >
          <VolIcon volume={volume} muted={muted} size={15} />
        </button>

        <div className="flex-1">
          <Slider
            value={displayVol}
            onChange={v => onVolumeChange(v)}
            onCommit={onVolumeCommit}
          />
        </div>

        <span className="text-[11px] text-white/50 w-[30px] text-right tabular-nums leading-none shrink-0">
          {displayVol}%
        </span>

        <button
          onClick={onGoSound}
          title="Sound settings"
          className="w-[30px] h-[30px] rounded-full bg-white/7 hover:bg-white/13 active:bg-white/5
            flex items-center justify-center shrink-0 transition-colors cursor-pointer"
        >
          <SlidersHorizontal size={13} color="rgba(255,255,255,0.55)" strokeWidth={1.75} />
        </button>
      </div>
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────────────────
// PAGE: Sound
// ─────────────────────────────────────────────────────────────────────────────

function SoundPage({
  devices, activeDeviceId, sessions, sessionVolumes, sessionMuted, sessionIcons,
  loading, onBack, onPickDevice, onSessionChange, onSessionCommit, onSessionMuteToggle,
}: {
  devices: AudioDevice[]
  activeDeviceId: string
  sessions: AudioSession[]
  sessionVolumes: Record<number, number>
  sessionMuted: Record<number, boolean>
  sessionIcons: Record<number, string | null>
  loading: boolean
  onBack(): void
  onPickDevice(id: string): void
  onSessionChange(pid: number, v: number): void
  onSessionCommit(pid: number, v: number): void
  onSessionMuteToggle(pid: number): void
}) {
  return (
    <div className="flex flex-col">
      <PageHeader title="Sound output" onBack={onBack} />

      <div className="drawer-scroll flex flex-col p-[12px] gap-[2px]" style={{ maxHeight: 360, overflowY: 'auto' }}>

        {/* Output device */}
        <p className="text-[10px] font-semibold text-white/35 uppercase tracking-widest px-[10px] py-[6px]">
          Output device
        </p>

        {/* Loading skeleton — shown while device list is empty (first open) */}
        {devices.length === 0 && loading && (
          <div className="flex flex-col gap-[4px] px-[2px]">
            {[0, 1].map(i => (
              <div key={i} className="flex items-center gap-[10px] px-[10px] py-[9px] rounded-[12px]"
                style={{ opacity: 0.35 - i * 0.1 }}>
                <div className="w-[3px] h-[18px] rounded-full bg-white/10 shrink-0" />
                <div className="w-[28px] h-[28px] rounded-full bg-white/8 shrink-0" />
                <div className="h-[10px] rounded-full bg-white/8 flex-1" />
              </div>
            ))}
          </div>
        )}

        {/* Staggered device entries */}
        <AnimatePresence>
          {devices.map((d, i) => {
            const active = d.id === activeDeviceId
            return (
              <motion.button
                key={d.id}
                initial={{ opacity: 0, y: -6 }}
                animate={{ opacity: 1, y: 0, transition: { duration: 0.18, ease: 'easeOut', delay: i * 0.055 } }}
                exit={{ opacity: 0, transition: { duration: 0.1 } }}
                onClick={() => onPickDevice(d.id)}
                className={`w-full flex items-center gap-[10px] px-[10px] py-[9px] rounded-[12px] transition-colors cursor-pointer text-left
                  ${active ? 'bg-white/10' : 'hover:bg-white/6'}`}
              >
                <div className={`w-[3px] h-[18px] rounded-full shrink-0 transition-colors ${active ? 'bg-[#3b82f6]' : 'bg-transparent'}`} />
                <div className="w-[28px] h-[28px] rounded-full bg-white/8 flex items-center justify-center shrink-0">
                  <DevIcon name={d.name} size={13} color={active ? 'white' : 'rgba(255,255,255,0.5)'} />
                </div>
                <span className={`flex-1 text-[12px] leading-none truncate ${active ? 'text-white font-medium' : 'text-white/65'}`}>
                  {d.name}
                </span>
                {active && <Check size={12} color="#60a5fa" strokeWidth={2.5} />}
              </motion.button>
            )
          })}
        </AnimatePresence>

        {/* Volume mixer */}
        <div className="h-[0.5px] bg-white/[0.06] my-[6px]" />

        <p className="text-[10px] font-semibold text-white/35 uppercase tracking-widest px-[10px] py-[6px]">
          Volume mixer
        </p>

        {loading && (
          <div className="flex items-center gap-[7px] px-[10px] py-[10px]">
            <motion.div animate={{ rotate: 360 }} transition={{ repeat: Infinity, duration: 0.9, ease: 'linear' }}>
              <RefreshCw size={11} color="rgba(255,255,255,0.3)" strokeWidth={2} />
            </motion.div>
            <span className="text-[11px] text-white/30">Loading apps…</span>
          </div>
        )}

        {!loading && sessions.length === 0 && (
          <p className="text-[11px] text-white/30 px-[10px] py-[6px]">No active audio apps</p>
        )}

        {sessions.map(s => {
          const vol = sessionVolumes[s.pid] ?? s.volume
          const isMuted = sessionMuted[s.pid] ?? s.muted
          const icon = sessionIcons[s.pid]
          const displayVol = isMuted ? 0 : vol
          return (
            <div key={s.pid} className="flex items-center gap-[10px] pr-[14px] py-[6px]">
              {/* App icon — mute/unmute button */}
              <button
                onClick={() => onSessionMuteToggle(s.pid)}
                title={isMuted ? 'Unmute' : 'Mute'}
                className={`w-[30px] h-[30px] rounded-full flex items-center justify-center shrink-0
                  transition-all duration-150 cursor-pointer
                  ${isMuted ? 'bg-white/5 opacity-45' : 'bg-white/7 hover:bg-white/12 active:bg-white/5'}`}
              >
                {icon
                  ? <img src={icon} className="w-[18px] h-[18px] object-contain rounded-[3px]"
                    style={{ filter: isMuted ? 'grayscale(1)' : 'none' }} alt={s.name} />
                  : <Speaker size={15} color={isMuted ? 'rgba(255,255,255,0.3)' : 'rgba(255,255,255,0.5)'} strokeWidth={1.75} />
                }
              </button>
              <div className="flex-1 min-w-0">
                <div className="flex items-center justify-between mb-[5px]">
                  <span className={`text-[11px] leading-none truncate pb-[1px] ${isMuted ? 'text-white/30' : 'text-white/55'}`}>{s.name}</span>
                  <span className="text-[10px] text-white/35 ml-[6px] shrink-0 tabular-nums leading-none">{displayVol}%</span>
                </div>
                <Slider
                  value={displayVol}
                  onChange={v => { if (isMuted && v > 0) onSessionMuteToggle(s.pid); onSessionChange(s.pid, v) }}
                  onCommit={v => onSessionCommit(s.pid, v)}
                />
              </div>
            </div>
          )
        })}
      </div>
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────────────────
// PAGE: Wi-Fi
// ─────────────────────────────────────────────────────────────────────────────

function WifiPage({
  network, networks, scanning, wifiEnabled, wifiToggling, connectingSSID,
  onBack, onWifiToggle, onScan, onConnect,
}: {
  network: NetworkState
  networks: WifiNetwork[]
  scanning: boolean
  wifiEnabled: boolean
  wifiToggling: boolean
  connectingSSID: string | null
  onBack(): void
  onWifiToggle(): void
  onScan(force: boolean): void
  onConnect(ssid: string): void
}) {
  return (
    <div className="flex flex-col">
      <PageHeader
        title="Wi-Fi"
        onBack={onBack}
        right={<Toggle on={wifiEnabled} onToggle={onWifiToggle} disabled={wifiToggling} />}
      />

      <div className="drawer-scroll flex flex-col gap-[2px] p-[10px]" style={{ maxHeight: 310, overflowY: 'auto' }}>
        {!wifiEnabled ? (
          <p className="text-[11px] text-white/30 text-center py-[24px]">Wi-Fi is turned off</p>
        ) : (
          <>
            <AnimatePresence initial={false}>
              {networks.map((n, i) => {
                const isConn = n.connected
                const isConnecting = connectingSSID === n.ssid
                const busyOther = connectingSSID !== null && !isConnecting && !isConn
                return (
                  <motion.button
                    layout="position"
                    key={n.ssid}
                    initial={{ opacity: 0, scale: 0.95 }}
                    animate={{ opacity: 1, scale: 1, transition: { ...SI } }}
                    exit={{ opacity: 0, scale: 0.95, transition: { duration: 0.1 } }}
                    onClick={() => !isConn && !connectingSSID && onConnect(n.ssid)}
                    disabled={busyOther}
                    className={`flex items-center gap-[10px] px-[10px] py-[10px] rounded-[12px] transition-colors cursor-pointer text-left group
                    ${isConn ? 'bg-[#3b82f6]/13' : isConnecting ? 'bg-white/8' : 'hover:bg-white/6'}
                    ${busyOther ? 'opacity-40 cursor-not-allowed' : ''}`}
                  >
                    <div className={`w-[32px] h-[32px] rounded-full flex items-center justify-center shrink-0
                    ${isConn ? 'bg-[#3b82f6]/25' : isConnecting ? 'bg-white/12' : 'bg-white/8'}`}
                    >
                      {isConnecting
                        ? <motion.div animate={{ rotate: 360 }} transition={{ repeat: Infinity, duration: 0.9, ease: 'linear' }}>
                          <RefreshCw size={14} color="rgba(255,255,255,0.7)" strokeWidth={2} />
                        </motion.div>
                        : <SigIcon signal={n.signal} size={14} color={isConn ? '#93c5fd' : 'rgba(255,255,255,0.6)'} />
                      }
                    </div>
                    <div className="flex flex-col gap-[2px] flex-1 min-w-0">
                      <span className={`text-[13px] font-medium leading-none truncate
                      ${isConn ? 'text-white' : isConnecting ? 'text-white/90' : 'text-white/80'}`}>
                        {n.ssid}
                      </span>
                      {isConn && (
                        <span className="text-[11px] text-white/40 leading-none">
                          Connected{n.secured ? ', secured' : ''}
                        </span>
                      )}
                      {isConnecting && (
                        <span className="text-[11px] text-white/40 leading-none">Connecting…</span>
                      )}
                    </div>
                    {n.secured && !isConn && !isConnecting && (
                      <KeyRound size={11} color="rgba(255,255,255,0.25)" strokeWidth={2} />
                    )}
                    {isConn
                      ? <Check size={13} color="#60a5fa" strokeWidth={2.5} />
                      : isConnecting
                        ? null
                        : <ChevronRight size={12} color="rgba(255,255,255,0.15)" strokeWidth={2}
                          className="opacity-0 group-hover:opacity-100 transition-opacity" />
                    }
                  </motion.button>
                )
              })}
            </AnimatePresence>

            {networks.length === 0 && !scanning && (
              <p className="text-[11px] text-white/25 text-center py-[20px]">No networks found</p>
            )}

            {scanning && (
              <div className="flex items-center justify-center gap-[7px] py-[14px]">
                <motion.div animate={{ rotate: 360 }} transition={{ repeat: Infinity, duration: 0.9, ease: 'linear' }}>
                  <RefreshCw size={12} color="rgba(255,255,255,0.3)" strokeWidth={2} />
                </motion.div>
                <span className="text-[11px] text-white/30">Scanning…</span>
              </div>
            )}
          </>
        )}
      </div>

      {wifiEnabled && (
        <div className="flex items-center justify-end gap-[6px] px-[14px] py-[9px] border-t border-white/[0.06]">
          <button
            onClick={() => onScan(true)}
            disabled={scanning}
            className="flex items-center gap-[6px] px-[10px] py-[5px] rounded-[8px]
              hover:bg-white/7 active:bg-white/4 transition-colors cursor-pointer disabled:opacity-40"
          >
            <motion.div
              animate={{ rotate: scanning ? 360 : 0 }}
              transition={{ repeat: scanning ? Infinity : 0, duration: 0.9, ease: 'linear' }}
            >
              <RefreshCw size={12} color="rgba(255,255,255,0.4)" strokeWidth={2} />
            </motion.div>
            <span className="text-[11px] text-white/40">Refresh</span>
          </button>
        </div>
      )}
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────────────────
// Root — manages all state + page navigation
// ─────────────────────────────────────────────────────────────────────────────

type Page = 'main' | 'sound' | 'wifi'

export function Drawer() {
  const [visible, setVisible] = useState(false)
  const [page, setPage] = useState<Page>('main')

  // ── System state (event-driven from Python monitors) ──────────────────────
  const [network, setNetwork] = useState<NetworkState>({ type: 'none', signal: null, ssid: null, hasInternet: false, vpnActive: false })
  const [volume, setVolume] = useState(50)
  const [muted, setMuted] = useState(false)

  // ── WiFi page state ───────────────────────────────────────────────────────
  const [wifiEnabled, setWifiEnabled] = useState(false)
  const [wifiToggling, setWifiToggling] = useState(false)
  const [networks, setNetworks] = useState<WifiNetwork[]>([])
  const [scanning, setScanning] = useState(false)
  const [connectingSSID, setConnectingSSID] = useState<string | null>(null)

  // ── Sound page state ──────────────────────────────────────────────────────
  const [devices, setDevices] = useState<AudioDevice[]>([])
  const [activeDeviceId, setActiveDevId] = useState('')
  const [sessions, setSessions] = useState<AudioSession[]>([])
  const [sessionVols, setSessionVols] = useState<Record<number, number>>({})
  const [sessionMuted, setSessionMuted] = useState<Record<number, boolean>>({})
  const [sessionIcons, setSessionIcons] = useState<Record<number, string | null>>({})
  const [loadingSessions, setLoadingSessions] = useState(false)

  const closeTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const scanningRef = useRef(false)   // mirror of `scanning` for interval callbacks

  const refreshWifiState = useCallback(() => {
    window.island.getWifiState().then(s => setWifiEnabled(s.enabled)).catch(() => { })
  }, [])

  // ── Close handler ─────────────────────────────────────────────────────────
  const closeDrawer = useCallback(() => {
    if (closeTimer.current !== null) return   // already animating out
    setVisible(false)
    closeTimer.current = setTimeout(() => {
      closeTimer.current = null
      window.island.closeDrawer()
    }, 210)
  }, [])

  // ── IPC subscriptions ──────────────────────────────────────────────────────
  useEffect(() => {
    const unsubShow = window.island.onDrawerShow(() => {
      if (closeTimer.current) { clearTimeout(closeTimer.current); closeTimer.current = null }
      setPage('main')
      setSessionVols({})
      setSessionMuted({})
      setSessionIcons({})
      setVisible(true)
      refreshWifiState()
    })

    const unsubForceClose = window.island.onDrawerForceClose(() => closeDrawer())

    window.island.getSystemStats().then(s => {
      setNetwork(s.network)
      setVolume(s.audio.volume)
      setMuted(s.audio.muted)
    })

    refreshWifiState()

    const unsubStats = window.island.onSystemStats(s => {
      setNetwork(s.network)
      setVolume(s.audio.volume)
      setMuted(s.audio.muted)
    })

    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') closeDrawer() }
    document.addEventListener('keydown', onKey)

    return () => {
      unsubShow()
      unsubForceClose()
      unsubStats()
      document.removeEventListener('keydown', onKey)
    }
  }, [])

  // ── Volume handlers ───────────────────────────────────────────────────────
  const volumeThrottle = useRef<number>(0)
  const handleVolumeChange = (v: number) => {
    setVolume(v)
    if (muted && v > 0) { setMuted(false); window.island.setSystemMute(false).catch(() => { }) }
    const now = Date.now()
    if (now - volumeThrottle.current >= 16) {
      volumeThrottle.current = now
      window.island.setSystemVolume(v).catch(() => { })
    }
  }
  const handleVolumeCommit = (v: number) => {
    // Guarantee final value lands even if last throttle window was skipped
    window.island.setSystemVolume(v).catch(() => { })
  }
  const handleMuteToggle = () => {
    const next = !muted
    setMuted(next)
    window.island.setSystemMute(next).catch(() => { })
  }

  // ── WiFi handlers ─────────────────────────────────────────────────────────
  const handleWifiToggle = async () => {
    if (wifiToggling) return
    setWifiToggling(true)
    const next = !wifiEnabled
    setWifiEnabled(next)
    try { await window.island.setWifiEnabled(next) } catch { setWifiEnabled(!next) }
    setWifiToggling(false)
    if (next) handleScan(false)
  }

  const handleScan = useCallback(async (force: boolean) => {
    if (force) {
      setScanning(true)
      scanningRef.current = true
    }
    try { setNetworks(await window.island.scanWifiNetworks(force) ?? []) } catch { /**/ }
    if (force) {
      setScanning(false)
      scanningRef.current = false
    }
  }, [])

  // ── Periodic background rescan while WiFi page is open ───────────────────
  // We need this because some WiFi adapters delay scanning the 5GHz band when
  // actively connected to a 2.4GHz band. The soft scans will effortlessly catch
  // the 5G network whenever Windows eventually discovers it in the background.
  useEffect(() => {
    if (page !== 'wifi') return
    const id = setInterval(() => {
      refreshWifiState()
      if (!wifiEnabled || scanningRef.current || connectingSSID) return
      handleScan(false)
    }, 7_000)
    return () => clearInterval(id)
  }, [page, wifiEnabled, connectingSSID, handleScan, refreshWifiState])

  const handleConnect = async (ssid: string) => {
    if (connectingSSID) return                      // already connecting
    setConnectingSSID(ssid)
    try {
      await window.island.connectWifi(ssid)
    } catch { /* best-effort */ }
    // Wait for Windows to establish the link, then refresh the list so the
    // newly connected network shows as connected automatically.
    await new Promise<void>(res => setTimeout(res, 5000))
    setConnectingSSID(null)
    handleScan(false)
  }

  // ── Sound handlers ────────────────────────────────────────────────────────
  const handlePickDevice = (id: string) => {
    setActiveDevId(id)
    window.island.setAudioDevice(id).catch(() => { })
  }

  const sessionThrottles = useRef<Record<number, number>>({})

  const handleSessionChange = (pid: number, v: number) => {
    setSessionVols(prev => ({ ...prev, [pid]: v }))
    const now = Date.now()
    const last = sessionThrottles.current[pid] ?? 0
    if (now - last >= 16) {
      sessionThrottles.current[pid] = now
      window.island.setSessionVolume(pid, v).catch(() => { })
    }
  }

  const handleSessionCommit = (pid: number, v: number) =>
    window.island.setSessionVolume(pid, v).catch(() => { })

  const handleSessionMuteToggle = (pid: number) => {
    const session = sessions.find(s => s.pid === pid)
    const current = sessionMuted[pid] ?? (session?.muted ?? false)
    const next = !current
    setSessionMuted(prev => ({ ...prev, [pid]: next }))
    window.island.setSessionVolume(pid, undefined, next).catch(() => { })
  }

  // ── Page navigation ───────────────────────────────────────────────────────
  const goTo = (p: Page) => {
    setPage(p)

    if (p === 'sound') {
      window.island.getAudioDevices()
        .then(r => { setDevices(r.devices); setActiveDevId(r.activeId) })
        .catch(() => { })
      setLoadingSessions(true)
      window.island.getAudioSessions()
        .then(s => {
          setSessions(s)
          setLoadingSessions(false)
          // Fetch per-app icons in parallel (best-effort)
          s.forEach(session => {
            window.island.getAppIcon(session.pid)
              .then(icon => setSessionIcons(prev => ({ ...prev, [session.pid]: icon })))
              .catch(() => { })
          })
        })
        .catch(() => { setLoadingSessions(false) })
    }

    if (p === 'wifi') {
      refreshWifiState()
      if (wifiEnabled) {
        // Trigger ONE hard scan when the panel opens. This forces the OS to start
        // a scan. The background interval (7s) will pick up the results shortly.
        handleScan(true)
      }
    }
  }

  const goBack = () => setPage('main')

  // ── Render ────────────────────────────────────────────────────────────────
  return (
    <div className="w-full h-full relative pointer-events-none select-none">

      {/* Click-outside backdrop — outside AnimatePresence so it has no animation and no key issues */}
      {visible && (
        <div
          className="absolute inset-0 pointer-events-auto"
          onMouseDown={closeDrawer}
        />
      )}

      <AnimatePresence>
        {visible && (
          <motion.div
            key="drawer-card"
            layout
            className="drawer-card absolute top-[6px] right-[8px] w-[320px] pointer-events-auto overflow-hidden"
            initial={{ opacity: 0, y: -12 }}
            animate={CARD_ENTER}
            exit={CARD_EXIT}
            onMouseDown={e => e.stopPropagation()}
            style={{
              background: '#000000',
              borderRadius: '18px',
            }}
            transition={{ layout: { type: 'spring', stiffness: 380, damping: 34, mass: 0.75 } }}
          >
            {/* Top inner sheen */}
            <div className="absolute top-0 left-0 right-0 h-[1px] rounded-t-[18px]
                bg-gradient-to-r from-transparent via-white/10 to-transparent pointer-events-none z-10" />

            <AnimatePresence mode="popLayout">

              {page === 'main' && (
                <motion.div key="main" initial={PAGE_IN} animate={PAGE_MID} exit={PAGE_OUT}>
                  <MainPage
                    volume={volume}
                    muted={muted}
                    network={network}
                    wifiEnabled={wifiEnabled}
                    wifiToggling={wifiToggling}
                    onVolumeChange={handleVolumeChange}
                    onVolumeCommit={handleVolumeCommit}
                    onMuteToggle={handleMuteToggle}
                    onWifiToggle={handleWifiToggle}
                    onGoSound={() => goTo('sound')}
                    onGoWifi={() => goTo('wifi')}
                  />
                </motion.div>
              )}

              {page === 'sound' && (
                <motion.div key="sound" initial={PAGE_IN} animate={PAGE_MID} exit={PAGE_OUT}>
                  <SoundPage
                    devices={devices}
                    activeDeviceId={activeDeviceId}
                    sessions={sessions}
                    sessionVolumes={sessionVols}
                    sessionMuted={sessionMuted}
                    sessionIcons={sessionIcons}
                    loading={loadingSessions}
                    onBack={goBack}
                    onPickDevice={handlePickDevice}
                    onSessionChange={handleSessionChange}
                    onSessionCommit={handleSessionCommit}
                    onSessionMuteToggle={handleSessionMuteToggle}
                  />
                </motion.div>
              )}

              {page === 'wifi' && (
                <motion.div key="wifi" initial={PAGE_IN} animate={PAGE_MID} exit={PAGE_OUT}>
                  <WifiPage
                    network={network}
                    networks={networks}
                    scanning={scanning}
                    wifiEnabled={wifiEnabled}
                    wifiToggling={wifiToggling}
                    connectingSSID={connectingSSID}
                    onBack={goBack}
                    onWifiToggle={handleWifiToggle}
                    onScan={handleScan}
                    onConnect={handleConnect}
                  />
                </motion.div>
              )}

            </AnimatePresence>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}
