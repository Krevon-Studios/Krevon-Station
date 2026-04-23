import { useEffect, useState } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import {
  Play, Pause, SkipBack, SkipForward,
  Check, Zap, Music2
} from 'lucide-react'
import { useIslandStore } from '../store/useIslandStore'
import type { IslandState } from '../types'

// ── Spring configs ────────────────────────────────────────────────────────────

const SPRING_PILL = { type: 'spring', stiffness: 400, damping: 38, mass: 0.8 } as const
const SPRING_CONTENT = { type: 'spring', stiffness: 320, damping: 32, mass: 0.7 } as const

// ── Sizes ─────────────────────────────────────────────────────────────────────

// Idle closed pill: very thin notch-like bar at top
const IDLE_CLOSED   = { w: 160, h: 32 }

// Expanded sizes per state (when hovered or active)
const EXPANDED: Record<IslandState['mode'], { w: number; h: number }> = {
  idle:          { w: 320, h: 72 },
  session_start: { w: 320, h: 80 },
  tool_active:   { w: 340, h: 80 },
  task_done:     { w: 360, h: 80 },
  media:         { w: 420, h: 110 },
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function sourceLabel(raw: string): string {
  const s = raw.toLowerCase().replace(/\.exe$/i, '')
  const map: Record<string, string> = {
    'spotifyab':        'Spotify',
    'spotify':          'Spotify',
    'chrome':           'Chrome',
    'firefox':          'Firefox',
    'msedge':           'Edge',
    'microsoft.edge':   'Edge',
    'opera':            'Opera',
    'brave':            'Brave',
    'windowsmediaplayer': 'Media Player',
    'vlc':              'VLC',
    'foobar2000':       'foobar2000',
    'itunes':           'iTunes',
    'tidal':            'TIDAL',
    'deezer':           'Deezer',
    'amazon':           'Amazon Music',
    'youtubemusic':     'YT Music',
  }
  for (const [k, v] of Object.entries(map)) {
    if (s.includes(k)) return v
  }
  return raw.replace(/\.exe$/i, '').split('.').pop() ?? raw
}

// ── Content: Idle ─────────────────────────────────────────────────────────────

function IdleExpandedContent() {
  const now = new Date()
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

function MediaExpandedContent({ state }: { state: Extract<IslandState, { mode: 'media' }> }) {
  const { session } = state
  const isPlaying = session.status === 'playing'

  const ctrl = (action: 'play-pause' | 'next' | 'prev') => (e: React.MouseEvent) => {
    e.stopPropagation()
    window.island.controlMedia(action, session.sourceAppId)
  }

  const skipColor = session.hasSkip ? 'rgba(255,255,255,0.7)' : 'rgba(255,255,255,0.2)'

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
        {session.thumbnail ? (
          <img
            src={session.thumbnail}
            alt=""
            className="w-full h-full rounded-[12px] object-cover"
            style={{ imageRendering: 'auto' }}
          />
        ) : (
          <div className={`w-full h-full rounded-[12px] flex items-center justify-center
            ${isPlaying ? 'bg-[#34D399]/12 border border-[#34D399]/20' : 'bg-white/8 border border-white/10'}`}>
            <Music2 size={22} color="rgba(255,255,255,0.3)" strokeWidth={1.5} />
          </div>
        )}

      </div>

      {/* Track info + controls */}
      <div className="flex flex-col gap-[10px] min-w-0 flex-1">
        {/* Source + settings row */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-[5px]">
            <Music2 size={9} color="rgba(255,255,255,0.3)" />
            <span className="text-[10px] text-white/30 tracking-wide">{sourceLabel(session.sourceAppId)}</span>
          </div>
          <div className={`w-[6px] h-[6px] rounded-full ${isPlaying ? 'bg-[#34D399]' : 'bg-white/20'}`}
            style={isPlaying ? { boxShadow: '0 0 6px #34D399' } : {}} />
        </div>

        {/* Title & artist */}
        <div className="flex flex-col gap-[3px] min-w-0">
          <span className="text-[14px] font-semibold text-white leading-none tracking-tight truncate">
            {session.title || 'Now Playing'}
          </span>
          {session.artist && (
            <span className="text-[11px] text-white/45 leading-none truncate">{session.artist}</span>
          )}
        </div>

        {/* Controls */}
        <div className="flex items-center gap-[6px]">
          <CtrlBtn onClick={ctrl('prev')} label="Previous" disabled={!session.hasSkip}>
            <SkipBack size={13} strokeWidth={2} color={skipColor} />
          </CtrlBtn>
          <CtrlBtn onClick={ctrl('play-pause')} label={isPlaying ? 'Pause' : 'Play'} primary>
            {isPlaying
              ? <Pause size={12} strokeWidth={2.2} color="white" />
              : <Play  size={12} strokeWidth={2.2} color="white" style={{ marginLeft: 1 }} />
            }
          </CtrlBtn>
          <CtrlBtn onClick={ctrl('next')} label="Next" disabled={!session.hasSkip}>
            <SkipForward size={13} strokeWidth={2} color={skipColor} />
          </CtrlBtn>
        </div>
      </div>
    </motion.div>
  )
}

// ── Closed pill content (tiny notch state) ────────────────────────────────────

function ClosedContent({ state }: { state: IslandState }) {
  const isMedia = state.mode === 'media'
  const isClaude = state.mode === 'tool_active' || state.mode === 'session_start' || state.mode === 'task_done'

  return (
    <motion.div
      key="closed"
      initial={{ opacity: 0 }}
      animate={{ opacity: 1, transition: { delay: 0.1, duration: 0.15 } }}
      exit={{ opacity: 0, transition: { duration: 0.08 } }}
      className="flex items-center gap-[6px] px-4"
    >
      {isClaude && (
        <span className="w-[5px] h-[5px] rounded-full bg-[#7C6AFF] shrink-0"
          style={{ boxShadow: '0 0 5px #7C6AFF' }} />
      )}
      {isMedia && (
        <span className="w-[5px] h-[5px] rounded-full bg-[#34D399] shrink-0"
          style={{ boxShadow: '0 0 5px #34D399' }} />
      )}
      {!isClaude && !isMedia && (
        <span className="w-[4px] h-[4px] rounded-full bg-white/20 shrink-0" />
      )}
    </motion.div>
  )
}

// ── Sub-components ────────────────────────────────────────────────────────────

function CtrlBtn({
  children, onClick, label, primary, disabled
}: {
  children:  React.ReactNode
  onClick:   (e: React.MouseEvent) => void
  label:     string
  primary?:  boolean
  disabled?: boolean
}) {
  return (
    <button
      onClick={disabled ? undefined : onClick}
      aria-label={label}
      aria-disabled={disabled}
      className={[
        'flex items-center justify-center rounded-full transition-all duration-150 cursor-default select-none',
        disabled ? 'opacity-30' : 'active:scale-90',
        primary
          ? 'w-[30px] h-[30px] bg-white/15 hover:bg-white/22 border border-white/10'
          : 'w-[26px] h-[26px] hover:bg-white/10',
      ].join(' ')}
      style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}
    >
      {children}
    </button>
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
  const { state } = useIslandStore()
  const [hovered, setHovered] = useState(false)

  const isExpanded = hovered
  const target     = isExpanded ? EXPANDED[state.mode] : IDLE_CLOSED

  // Hover state is driven entirely by the main process via screen.getCursorScreenPoint()
  // polling — no renderer-side mousemove math or getBoundingClientRect needed.
  // Update the hit-box in the main process when the target size changes.
  // This prevents the "invisible boundary" issue where the main process thinks
  // the entire transparent Electron window is the hover zone.
  useEffect(() => {
    window.island.setHitBox(target.w, target.h)
  }, [target.w, target.h])

  useEffect(() => {
    window.island.onHover((over) => setHovered(over))
    return () => window.island.removeAllListeners()
  }, [])

  return (
    <div
      className="w-full flex justify-center select-none"
      style={{ background: 'transparent' }}
    >
      <motion.div
        animate={{
          width: target.w,
          height: target.h,
          borderRadius: isExpanded ? '0px 0px 22px 22px' : '0px 0px 14px 14px',
        }}
        transition={SPRING_PILL}
        style={{
          background: '#000000',
          boxShadow: [
            '0 8px 32px rgba(0,0,0,0.55)',
            '0 2px 8px rgba(0,0,0,0.4)',
          ].join(', '),
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
              <MediaExpandedContent key="media-exp" state={state} />
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

