import { useEffect, useState } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import {
  Play, Pause,
  Check, Zap, Music2, ChevronLeft, ChevronRight
} from 'lucide-react'
import { useIslandStore } from '../store/useIslandStore'
import type { IslandState } from '../types'

// ── Spring configs ────────────────────────────────────────────────────────────

const SPRING_PILL = { type: 'spring', stiffness: 400, damping: 38, mass: 0.8 } as const
const SPRING_CONTENT = { type: 'spring', stiffness: 320, damping: 32, mass: 0.7 } as const

// ── Sizes ─────────────────────────────────────────────────────────────────────

// Idle closed pill sizes per state
const CLOSED_SIZES: Record<IslandState['mode'], { w: number; h: number }> = {
  idle: { w: 210, h: 32 },
  session_start: { w: 210, h: 32 },
  tool_active: { w: 210, h: 32 },
  task_done: { w: 210, h: 32 },
  media: { w: 240, h: 32 },
}

// Expanded sizes per state (when hovered or active)
const EXPANDED: Record<IslandState['mode'], { w: number; h: number }> = {
  idle: { w: 320, h: 72 },
  session_start: { w: 320, h: 80 },
  tool_active: { w: 340, h: 80 },
  task_done: { w: 360, h: 80 },
  media: { w: 420, h: 110 },
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function sourceLabel(raw: string): string {
  const s = raw.toLowerCase().replace(/\.exe$/i, '')
  const map: Record<string, string> = {
    'whatsapp': 'WhatsApp',
    'spotifyab': 'Spotify',
    'spotify': 'Spotify',
    'chrome': 'Chrome',
    'firefox': 'Firefox',
    'msedge': 'Edge',
    'microsoft.edge': 'Edge',
    'opera': 'Opera',
    'brave': 'Brave',
    'windowsmediaplayer': 'Media Player',
    'vlc': 'VLC',
    'foobar2000': 'foobar2000',
    'itunes': 'iTunes',
    'tidal': 'TIDAL',
    'deezer': 'Deezer',
    'amazon': 'Amazon Music',
    'youtubemusic': 'YT Music',
  }
  for (const [k, v] of Object.entries(map)) {
    if (s.includes(k)) return v
  }
  return raw.replace(/\.exe$/i, '').split('.').pop() ?? raw
}

// ── Content: Idle ─────────────────────────────────────────────────────────────

function IdleExpandedContent() {
  const [now, setNow] = useState(() => new Date())
  useEffect(() => {
    const timer = setInterval(() => setNow(new Date()), 1000)
    return () => clearInterval(timer)
  }, [])

  const time = now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
  const date = now.toLocaleDateString([], { weekday: 'short', month: 'short', day: 'numeric' })

  return (
    <motion.div
      key="idle-exp"
      initial={{ opacity: 0, y: -6 }}
      animate={{ opacity: 1, y: 0, transition: { ...SPRING_CONTENT, delay: 0.06 } }}
      exit={{ opacity: 0, y: -4, transition: { duration: 0.12 } }}
      className="flex items-center justify-between w-full px-5"
    >
      <div className="flex flex-col gap-[2px]">
        <span className="text-[22px] font-bold text-white leading-none tracking-tight">{time}</span>
        <span className="text-[11px] text-white/40 leading-none tracking-wide">{date}</span>
      </div>
      <div className="flex items-center gap-[6px]">
        <div className="w-[2px] h-[2px] rounded-full bg-[#7C6AFF]" />
        <span className="text-[11px] text-white/30 tracking-wide">Dynamic Island</span>
      </div>
    </motion.div>
  )
}

// ── Content: Session Start ─────────────────────────────────────────────────────

function SessionExpandedContent() {
  return (
    <motion.div
      key="session-exp"
      initial={{ opacity: 0, y: -6 }}
      animate={{ opacity: 1, y: 0, transition: { ...SPRING_CONTENT, delay: 0.06 } }}
      exit={{ opacity: 0, y: -4, transition: { duration: 0.12 } }}
      className="flex items-center gap-[14px] w-full px-5"
    >
      <div className="w-[36px] h-[36px] rounded-full bg-[#7C6AFF]/15 border border-[#7C6AFF]/30 flex items-center justify-center shrink-0">
        <Zap size={16} strokeWidth={2} color="#7C6AFF" />
      </div>
      <div className="flex flex-col gap-[5px]">
        <span className="text-[14px] font-semibold text-white leading-none tracking-tight">Claude Code</span>
        <span className="text-[11px] text-white/40 leading-none">Session started</span>
      </div>
      <div className="ml-auto">
        <span className="text-[10px] text-[#7C6AFF]/60 font-mono">LIVE</span>
      </div>
    </motion.div>
  )
}

// ── Content: Tool Active ───────────────────────────────────────────────────────

function ToolExpandedContent({ state }: { state: Extract<IslandState, { mode: 'tool_active' }> }) {
  const sub = state.toolName.length > 30 ? state.toolName.slice(0, 28) + '…' : state.toolName
  return (
    <motion.div
      key="tool-exp"
      initial={{ opacity: 0, y: -6 }}
      animate={{ opacity: 1, y: 0, transition: { ...SPRING_CONTENT, delay: 0.06 } }}
      exit={{ opacity: 0, y: -4, transition: { duration: 0.12 } }}
      className="flex items-center gap-[14px] w-full px-5"
    >
      <PulsingOrb />
      <div className="flex flex-col gap-[5px] min-w-0">
        <span className="text-[14px] font-semibold text-white leading-none tracking-tight whitespace-nowrap">
          {state.displayLabel}
        </span>
        <span className="text-[10px] text-white/35 font-mono leading-none whitespace-nowrap">{sub}</span>
      </div>
    </motion.div>
  )
}

// ── Content: Task Done ─────────────────────────────────────────────────────────

function DoneExpandedContent({ state }: { state: Extract<IslandState, { mode: 'task_done' }> }) {
  const secs = state.durationMs > 0 ? (state.durationMs / 1000).toFixed(1) + 's' : null
  const cost = state.cost <= 0 ? null
    : state.cost < 0.001 ? '<$0.001'
      : `$${state.cost.toFixed(3)}`
  return (
    <motion.div
      key="done-exp"
      initial={{ opacity: 0, y: -6 }}
      animate={{ opacity: 1, y: 0, transition: { ...SPRING_CONTENT, delay: 0.06 } }}
      exit={{ opacity: 0, y: -4, transition: { duration: 0.12 } }}
      className="flex items-center gap-[14px] w-full px-5"
    >
      <div className="w-[36px] h-[36px] rounded-full bg-[#34D399]/12 border border-[#34D399]/25 flex items-center justify-center shrink-0">
        <Check size={16} strokeWidth={2.5} color="#34D399" />
      </div>
      <div className="flex flex-col gap-[5px]">
        <span className="text-[14px] font-semibold text-[#34D399] leading-none tracking-tight">Task complete</span>
        <div className="flex items-center gap-[7px] text-[11px] text-white/40 leading-none">
          <span>{state.turns} turn{state.turns !== 1 ? 's' : ''}</span>
          {secs && <><Sep /><span>{secs}</span></>}
          {cost && <><Sep /><span className="text-[#34D399]/70">{cost}</span></>}
        </div>
      </div>
    </motion.div>
  )
}

// ── Content: Media ─────────────────────────────────────────────────────────────

function MediaExpandedContent({
  state, nextMedia, prevMedia, setMediaIndex
}: {
  state: Extract<IslandState, { mode: 'media' }>
  nextMedia: () => void
  prevMedia: () => void
  setMediaIndex: (index: number) => void
}) {
  const { session } = state
  const isPlaying = session.status === 'playing'

  const ctrl = (action: 'play-pause' | 'next' | 'prev') => (e: React.MouseEvent) => {
    e.stopPropagation()
    window.island.controlMedia(action, session.sourceAppId)
  }

  return (
    <motion.div
      key="media-exp"
      initial={{ opacity: 0, y: -8 }}
      animate={{ opacity: 1, y: 0, transition: { ...SPRING_CONTENT, delay: 0.07 } }}
      exit={{ opacity: 0, y: -4, transition: { duration: 0.12 } }}
      className="flex items-center w-full px-4 gap-3"
    >
      {/* Album art */}
      <div className="relative w-[72px] h-[72px] shrink-0">
        <AnimatePresence mode="wait">
          <motion.div
            key={session.sourceAppId}
            initial={{ opacity: 0, scale: 0.9 }}
            animate={{ opacity: 1, scale: 1 }}
            exit={{ opacity: 0, scale: 0.9 }}
            transition={{ duration: 0.15 }}
            className="w-full h-full"
          >
            {session.thumbnail ? (
              <img
                src={session.thumbnail}
                alt=""
                className="w-full h-full rounded-[12px] object-cover"
                style={{ imageRendering: 'auto' }}
              />
            ) : (
              <div className="w-full h-full rounded-[12px] flex items-center justify-center bg-white/8 border border-white/10">
                <Music2 size={22} color="rgba(255,255,255,0.3)" strokeWidth={1.5} />
              </div>
            )}
          </motion.div>
        </AnimatePresence>
      </div>

      {/* Track info + controls */}
      <div className="flex flex-col gap-[10px] min-w-0 flex-1">
        {/* Source + settings row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-[6px]">
            <span className="text-[10px] text-white/40 tracking-wide font-medium">
              {sourceLabel(session.sourceAppId)}
            </span>
            {state.sessions?.length > 1 && (
              <PaginationDots count={state.sessions.length} activeIndex={state.activeIndex} setIndex={setMediaIndex} />
            )}
          </div>
          <div className={`w-[6px] h-[6px] rounded-full ${isPlaying ? 'bg-[#34D399]' : 'bg-white/20'}`}
            style={isPlaying ? { boxShadow: '0 0 6px #34D399' } : {}} />
        </div>

        {/* Title & artist */}
        <div className="flex flex-col gap-[3px] min-w-0 h-[30px] justify-center">
          <AnimatePresence mode="wait">
            <motion.div
              key={session.sourceAppId + session.title}
              initial={{ opacity: 0, y: 5 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -5 }}
              transition={{ duration: 0.15 }}
              className="flex flex-col gap-[3px] min-w-0"
            >
              <span className="text-[14px] font-semibold text-white leading-none tracking-tight truncate">
                {session.title || 'Now Playing'}
              </span>
              {session.artist && (
                <span className="text-[11px] text-white/45 leading-none truncate">{session.artist}</span>
              )}
            </motion.div>
          </AnimatePresence>
        </div>

        {/* Controls */}
        <div className="flex items-center gap-[12px] mt-1">
          <CtrlBtn onClick={ctrl('prev')} label="Previous" disabled={!session.hasSkip}>
            <PrevIcon size={14} />
          </CtrlBtn>
          <CtrlBtn onClick={ctrl('play-pause')} label={isPlaying ? 'Pause' : 'Play'}>
            {isPlaying
              ? <Pause size={20} fill="white" color="white" />
              : <Play size={20} fill="white" color="white" />
            }
          </CtrlBtn>
          <CtrlBtn onClick={ctrl('next')} label="Next" disabled={!session.hasSkip}>
            <NextIcon size={14} />
          </CtrlBtn>
        </div>
      </div>
    </motion.div>
  )
}

// ── Closed pill content (tiny notch state) ────────────────────────────────────

function ClosedContent({ state }: { state: IslandState }) {
  const isClaude = state.mode === 'tool_active' || state.mode === 'session_start' || state.mode === 'task_done'
  const isPlayingMedia = state.mode === 'media' && state.session.status === 'playing'

  const [now, setNow] = useState(() => new Date())
  useEffect(() => {
    const timer = setInterval(() => setNow(new Date()), 1000)
    return () => clearInterval(timer)
  }, [])

  if (isPlayingMedia) {
    const mediaState = state as Extract<IslandState, { mode: 'media' }>
    const { session } = mediaState
    return (
      <motion.div
        key="closed-media"
        initial={{ opacity: 0 }}
        animate={{ opacity: 1, transition: { delay: 0.1, duration: 0.15 } }}
        exit={{ opacity: 0, transition: { duration: 0.08 } }}
        className="flex items-center gap-[10px] px-5 w-full h-full"
      >
        <TinyVisualizer isPlaying={true} />
        <span className="text-[12px] text-white/90 font-medium truncate leading-none mt-[1px]">
          {session.title || 'Unknown Media'}
        </span>
      </motion.div>
    )
  }

  const time = now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
  const date = now.toLocaleDateString([], { weekday: 'short', month: 'short', day: 'numeric' })

  return (
    <motion.div
      key="closed-default"
      initial={{ opacity: 0 }}
      animate={{ opacity: 1, transition: { delay: 0.1, duration: 0.15 } }}
      exit={{ opacity: 0, transition: { duration: 0.08 } }}
      className="flex items-center justify-center w-full h-full"
    >
      {isClaude ? (
        <span className="w-[5px] h-[5px] rounded-full bg-[#7C6AFF] shrink-0"
          style={{ boxShadow: '0 0 5px #7C6AFF' }} />
      ) : (
        <div className="flex items-center justify-center gap-[8px] text-white w-full px-4">
          <span className="text-[12px] font-medium tracking-wide whitespace-nowrap">{date}</span>
          <span className="text-[12px] font-medium tracking-wide whitespace-nowrap">{time}</span>
        </div>
      )}
    </motion.div>
  )
}

// ── Sub-components ────────────────────────────────────────────────────────────

function PaginationDots({ count, activeIndex, setIndex }: { count: number, activeIndex: number, setIndex: (i: number) => void }) {
  return (
    <div className="flex items-center ml-1" style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}>
      {Array.from({ length: count }).map((_, i) => (
        <button
          key={i}
          onClick={(e) => { e.stopPropagation(); setIndex(i); }}
          className="py-[4px] px-[2px] group"
        >
          <div className={`h-[5px] rounded-full transition-all duration-300 ${
            i === activeIndex ? 'w-[14px] bg-white' : 'w-[5px] bg-white/30 group-hover:bg-white/60'
          }`} />
        </button>
      ))}
    </div>
  )
}

function TinyVisualizer({ isPlaying }: { isPlaying: boolean }) {
  return (
    <div className="flex items-center gap-[2px] h-[12px] shrink-0">
      <motion.div
        className="w-[2px] bg-[#34D399] rounded-full"
        animate={{ height: isPlaying ? [4, 10, 4, 12, 4] : 3 }}
        transition={{ repeat: Infinity, duration: 1.1, ease: "easeInOut" }}
      />
      <motion.div
        className="w-[2px] bg-[#34D399] rounded-full"
        animate={{ height: isPlaying ? [6, 4, 12, 6, 6] : 3 }}
        transition={{ repeat: Infinity, duration: 1.3, ease: "easeInOut" }}
      />
      <motion.div
        className="w-[2px] bg-[#34D399] rounded-full"
        animate={{ height: isPlaying ? [4, 12, 4, 8, 4] : 3 }}
        transition={{ repeat: Infinity, duration: 1.2, ease: "easeInOut" }}
      />
      <motion.div
        className="w-[2px] bg-[#34D399] rounded-full"
        animate={{ height: isPlaying ? [8, 4, 10, 4, 8] : 3 }}
        transition={{ repeat: Infinity, duration: 1.4, ease: "easeInOut" }}
      />
    </div>
  )
}

function CtrlBtn({
  children, onClick, label, disabled
}: {
  children: React.ReactNode
  onClick: (e: React.MouseEvent) => void
  label: string
  disabled?: boolean
}) {
  return (
    <button
      onClick={disabled ? undefined : onClick}
      aria-label={label}
      aria-disabled={disabled}
      className={[
        'flex items-center justify-center rounded-full transition-all duration-150 cursor-default select-none',
        disabled ? 'opacity-30' : 'active:scale-90 opacity-80 hover:opacity-100',
      ].join(' ')}
      style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}
    >
      {children}
    </button>
  )
}



function NextIcon({ size }: { size: number }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width={size} height={size} viewBox="0 0 24 24" fill="white">
      <path stroke="none" d="M0 0h24v24H0z" fill="none" />
      <path d="M2 5v14c0 .86 1.012 1.318 1.659 .753l8 -7a1 1 0 0 0 0 -1.506l-8 -7c-.647 -.565 -1.659 -.106 -1.659 .753z" />
      <path d="M13 5v14c0 .86 1.012 1.318 1.659 .753l8 -7a1 1 0 0 0 0 -1.506l-8 -7c-.647 -.565 -1.659 -.106 -1.659 .753z" />
    </svg>
  )
}

function PrevIcon({ size }: { size: number }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width={size} height={size} viewBox="0 0 24 24" fill="white">
      <path stroke="none" d="M0 0h24v24H0z" fill="none" />
      <path d="M20.341 4.247l-8 7a1 1 0 0 0 0 1.506l8 7c.647 .565 1.659 .106 1.659 -.753v-14c0 -.86 -1.012 -1.318 -1.659 -.753z" />
      <path d="M9.341 4.247l-8 7a1 1 0 0 0 0 1.506l8 7c.647 .565 1.659 .106 1.659 -.753v-14c0 -.86 -1.012 -1.318 -1.659 -.753z" />
    </svg>
  )
}

function PulsingOrb() {
  return (
    <div className="relative w-[36px] h-[36px] shrink-0 flex items-center justify-center">
      <span className="absolute inset-0 rounded-full bg-[#7C6AFF]/20 animate-ping" style={{ animationDuration: '1.4s' }} />
      <span className="relative w-[28px] h-[28px] rounded-full bg-[#7C6AFF]/15 border border-[#7C6AFF]/30 flex items-center justify-center">
        <Zap size={12} strokeWidth={2.2} color="#7C6AFF" />
      </span>
    </div>
  )
}

function Sep() {
  return <span className="w-[2px] h-[2px] rounded-full bg-white/20 inline-block align-middle shrink-0" />
}

export function Island() {
  const { state, nextMedia, prevMedia, setMediaIndex } = useIslandStore()
  const [hovered, setHovered] = useState(false)

  const isExpanded = hovered

  let closedTarget = CLOSED_SIZES[state.mode]
  if (state.mode === 'media' && state.session.status !== 'playing') {
    closedTarget = CLOSED_SIZES['idle']
  }

  const target = isExpanded ? EXPANDED[state.mode] : closedTarget

  // Hover state is driven entirely by the main process via screen.getCursorScreenPoint()
  // polling — no renderer-side mousemove math or getBoundingClientRect needed.
  // Update the hit-box in the main process when the target size changes.
  // This prevents the "invisible boundary" issue where the main process thinks
  // the entire transparent Electron window is the hover zone.
  useEffect(() => {
    window.island.setHitBox(target.w, target.h)
  }, [target.w, target.h])

  useEffect(() => {
    return window.island.onHover((over) => setHovered(over))
  }, [])

  return (
    <div
      className="w-full flex justify-center select-none pointer-events-none"
      style={{ background: 'transparent' }}
    >
      <motion.div
        className="pointer-events-auto"
        animate={{
          width: target.w,
          height: target.h,
          borderRadius: isExpanded ? '0px 0px 22px 22px' : '0px 0px 14px 14px',
          boxShadow: isExpanded
            ? '0 8px 32px rgba(0,0,0,0.55), 0 2px 8px rgba(0,0,0,0.40)'
            : '0 8px 32px rgba(0,0,0,0.00), 0 2px 8px rgba(0,0,0,0.00)',
        }}
        transition={SPRING_PILL}
        style={{
          background: '#000000',
          overflow: 'hidden',
          position: 'relative',
          cursor: 'default',
        }}
      >
        {/* Content */}
        <div className="w-full h-full flex items-center">
          <AnimatePresence mode="wait">
            {!isExpanded ? (
              <ClosedContent key={`closed-${state.mode}`} state={state} />
            ) : state.mode === 'idle' ? (
              <IdleExpandedContent key="idle-exp" />
            ) : state.mode === 'session_start' ? (
              <SessionExpandedContent key="session-exp" />
            ) : state.mode === 'tool_active' ? (
              <ToolExpandedContent key="tool-exp" state={state} />
            ) : state.mode === 'task_done' ? (
              <DoneExpandedContent key="done-exp" state={state} />
            ) : state.mode === 'media' ? (
              <MediaExpandedContent key="media-exp" state={state} nextMedia={nextMedia} prevMedia={prevMedia} setMediaIndex={setMediaIndex} />
            ) : null}
          </AnimatePresence>
        </div>

        {/* State glow overlays */}
        {(state.mode === 'tool_active' || state.mode === 'session_start') && (
          <div className="absolute inset-0 pointer-events-none"
            style={{ background: 'radial-gradient(ellipse at 50% 0%, rgba(124,106,255,0.06) 0%, transparent 70%)' }} />
        )}
        {state.mode === 'task_done' && (
          <div className="absolute inset-0 pointer-events-none"
            style={{ background: 'radial-gradient(ellipse at 50% 0%, rgba(52,211,153,0.06) 0%, transparent 70%)' }} />
        )}
        {state.mode === 'media' && isExpanded && (
          <div className="absolute inset-0 pointer-events-none"
            style={{ background: 'radial-gradient(ellipse at 20% 50%, rgba(29,185,84,0.04) 0%, transparent 60%)' }} />
        )}
      </motion.div>
    </div>
  )
}

