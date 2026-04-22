import { useRef, useEffect } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import {
  Play, Pause, SkipBack, SkipForward,
  Check, Zap, Music2
} from 'lucide-react'
import { useIslandStore } from '../store/useIslandStore'
import type { IslandState } from '../types'

const fade = {
  enter:   { opacity: 0, scale: 0.9,  filter: 'blur(4px)' },
  visible: { opacity: 1, scale: 1,    filter: 'blur(0px)',
    transition: { duration: 0.18, ease: [0.16, 1, 0.3, 1] } },
  exit:    { opacity: 0, scale: 0.92, filter: 'blur(2px)',
    transition: { duration: 0.1 } }
}

// Map raw AUMID → display name
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

// ── State content ────────────────────────────────────────────────────────────

function IdleContent() {
  return (
    <motion.div key="idle" variants={fade} initial="enter" animate="visible" exit="exit"
      className="flex items-center gap-[7px] px-4">
      <span className="w-[5px] h-[5px] rounded-full bg-[#7C6AFF]/60 shrink-0" />
      <span className="text-[11px] font-medium tracking-wide text-white/40 select-none whitespace-nowrap">
        Dynamic Island
      </span>
    </motion.div>
  )
}

function SessionContent({ state }: { state: Extract<IslandState, { mode: 'session_start' }> }) {
  void state
  return (
    <motion.div key="session" variants={fade} initial="enter" animate="visible" exit="exit"
      className="flex items-center gap-[10px] px-4">
      <ClaudeIcon />
      <div className="flex flex-col gap-[3px]">
        <span className="text-[12px] font-semibold text-white leading-none tracking-tight">Claude Code</span>
        <span className="text-[10px] text-white/40 leading-none">Session started</span>
      </div>
    </motion.div>
  )
}

function ToolContent({ state }: { state: Extract<IslandState, { mode: 'tool_active' }> }) {
  const sub = state.toolName.length > 28 ? state.toolName.slice(0, 26) + '…' : state.toolName
  return (
    <motion.div key="tool" variants={fade} initial="enter" animate="visible" exit="exit"
      className="flex items-center gap-[10px] px-4">
      <PulsingDot />
      <div className="flex flex-col gap-[3px]">
        <span className="text-[12px] font-semibold text-white leading-none tracking-tight whitespace-nowrap">
          {state.displayLabel}
        </span>
        <span className="text-[10px] text-white/35 font-mono leading-none whitespace-nowrap">{sub}</span>
      </div>
    </motion.div>
  )
}

function DoneContent({ state }: { state: Extract<IslandState, { mode: 'task_done' }> }) {
  const secs = state.durationMs > 0 ? (state.durationMs / 1000).toFixed(1) + 's' : null
  const cost = state.cost <= 0 ? null
    : state.cost < 0.001 ? '<$0.001'
    : `$${state.cost.toFixed(3)}`
  return (
    <motion.div key="done" variants={fade} initial="enter" animate="visible" exit="exit"
      className="flex items-center gap-[10px] px-4">
      <div className="w-[20px] h-[20px] rounded-full bg-[#34D399]/12 border border-[#34D399]/20 flex items-center justify-center shrink-0">
        <Check size={10} strokeWidth={2.5} color="#34D399" />
      </div>
      <div className="flex flex-col gap-[3px]">
        <span className="text-[12px] font-semibold text-[#34D399] leading-none tracking-tight">Task complete</span>
        <div className="flex items-center gap-[6px] text-[10px] text-white/40 leading-none">
          <span>{state.turns} turn{state.turns !== 1 ? 's' : ''}</span>
          {secs && <><Sep /><span>{secs}</span></>}
          {cost && <><Sep /><span>{cost}</span></>}
        </div>
      </div>
    </motion.div>
  )
}

function MediaContent({ state }: { state: Extract<IslandState, { mode: 'media' }> }) {
  const { session } = state
  const isPlaying = session.status === 'playing'
  const title     = session.title.length  > 22 ? session.title.slice(0, 20)  + '…' : session.title
  const artist    = session.artist.length > 18 ? session.artist.slice(0, 16) + '…' : session.artist

  const ctrl = (action: 'play-pause' | 'next' | 'prev') => (e: React.MouseEvent) => {
    e.stopPropagation()
    window.island.controlMedia(action, session.sourceAppId)
  }

  const skipColor = session.hasSkip ? 'rgba(255,255,255,0.65)' : 'rgba(255,255,255,0.18)'

  return (
    <motion.div key="media" variants={fade} initial="enter" animate="visible" exit="exit"
      className="flex items-center justify-between w-full px-[14px] gap-3">

      {/* Left — thumbnail/icon + track info */}
      <div className="flex items-center gap-[9px] min-w-0">
        <div className="relative w-[30px] h-[30px] shrink-0">
          {session.thumbnail ? (
            <img
              src={session.thumbnail}
              alt=""
              className="w-full h-full rounded-[5px] object-cover"
              style={{ imageRendering: 'auto' }}
            />
          ) : (
            <div className={`w-full h-full rounded-[5px] flex items-center justify-center border
              ${isPlaying ? 'bg-[#34D399]/12 border-[#34D399]/20' : 'bg-white/5 border-white/10'}`}>
              <Music2 size={12} color="rgba(255,255,255,0.3)" strokeWidth={1.8} />
            </div>
          )}
          {/* playing indicator dot overlaid on thumbnail */}
          {session.thumbnail && isPlaying && (
            <div className="absolute -bottom-[2px] -right-[2px] w-[8px] h-[8px] rounded-full bg-[#060606] flex items-center justify-center">
              <span className="w-[5px] h-[5px] rounded-full bg-[#34D399]" />
            </div>
          )}
        </div>
        <div className="flex flex-col gap-[3px] min-w-0">
          <span className="text-[12px] font-semibold text-white leading-none tracking-tight whitespace-nowrap">
            {title || 'Now Playing'}
          </span>
          <div className="flex items-center gap-[5px] text-[10px] leading-none">
            {artist && <span className="text-white/40 whitespace-nowrap">{artist}</span>}
            {artist && <Sep />}
            <span className="text-white/25 whitespace-nowrap">{sourceLabel(session.sourceAppId)}</span>
          </div>
        </div>
      </div>

      {/* Right — playback controls */}
      <div className="flex items-center gap-[2px] shrink-0">
        <CtrlBtn onClick={ctrl('prev')} label="Previous track" disabled={!session.hasSkip}>
          <SkipBack size={12} strokeWidth={2} color={skipColor} />
        </CtrlBtn>
        <CtrlBtn onClick={ctrl('play-pause')} label={isPlaying ? 'Pause' : 'Play'} primary>
          {isPlaying
            ? <Pause size={11} strokeWidth={2.2} color="white" />
            : <Play  size={11} strokeWidth={2.2} color="white" />
          }
        </CtrlBtn>
        <CtrlBtn onClick={ctrl('next')} label="Next track" disabled={!session.hasSkip}>
          <SkipForward size={12} strokeWidth={2} color={skipColor} />
        </CtrlBtn>
      </div>
    </motion.div>
  )
}

// ── Sub-components ───────────────────────────────────────────────────────────

function CtrlBtn({
  children, onClick, label, primary, small, disabled
}: {
  children:  React.ReactNode
  onClick:   (e: React.MouseEvent) => void
  label:     string
  primary?:  boolean
  small?:    boolean
  disabled?: boolean
}) {
  return (
    <button
      onClick={disabled ? undefined : onClick}
      aria-label={label}
      aria-disabled={disabled}
      className={[
        'flex items-center justify-center rounded-full transition-all duration-100 cursor-default',
        disabled ? 'opacity-40' : 'active:scale-90',
        primary ? 'w-[26px] h-[26px] bg-white/12 hover:bg-white/18'
        : small  ? 'w-[14px] h-[14px] hover:bg-white/10'
        :          'w-[22px] h-[22px] hover:bg-white/8',
      ].join(' ')}
      style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}
    >
      {children}
    </button>
  )
}


function ClaudeIcon() {
  return (
    <div className="w-[20px] h-[20px] rounded-full bg-[#7C6AFF]/15 border border-[#7C6AFF]/25 flex items-center justify-center shrink-0">
      <Zap size={10} strokeWidth={2.2} color="#7C6AFF" />
    </div>
  )
}

function PulsingDot() {
  return (
    <div className="relative w-[8px] h-[8px] shrink-0">
      <span className="absolute inset-0 rounded-full bg-[#7C6AFF] animate-ping opacity-50" />
      <span className="relative block w-full h-full rounded-full bg-[#7C6AFF]" />
    </div>
  )
}

function Sep() {
  return <span className="w-[2.5px] h-[2.5px] rounded-full bg-white/20 inline-block align-middle shrink-0" />
}

// ── Main island ──────────────────────────────────────────────────────────────

export function Island() {
  const { state } = useIslandStore()
  const pillRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const onMove = (e: MouseEvent) => {
      if (!pillRef.current) return
      const r = pillRef.current.getBoundingClientRect()
      const over = e.clientX >= r.left && e.clientX <= r.right &&
                   e.clientY >= r.top  && e.clientY <= r.bottom
      window.island.setIgnoreMouse(!over)
    }
    document.addEventListener('mousemove', onMove)
    return () => document.removeEventListener('mousemove', onMove)
  }, [])

  return (
    <div className="w-full h-full flex items-center justify-center" style={{ background: 'transparent' }}>
      <div
        ref={pillRef}
        className="flex items-center justify-center select-none overflow-hidden"
        style={{
          width:        'calc(100% - 8px)',
          height:       'calc(100% - 8px)',
          borderRadius: '999px',
          background:   '#060606',
          boxShadow:    'inset 0 1px 0 rgba(255,255,255,0.08), inset 0 -1px 0 rgba(255,255,255,0.02)',
        }}
      >
        <AnimatePresence mode="wait">
          {state.mode === 'idle'          && <IdleContent    key="idle" />}
          {state.mode === 'session_start' && <SessionContent key="session" state={state} />}
          {state.mode === 'tool_active'   && <ToolContent    key="tool"    state={state} />}
          {state.mode === 'task_done'     && <DoneContent    key="done"    state={state} />}
          {state.mode === 'media'         && <MediaContent key="media" state={state} />}
        </AnimatePresence>
      </div>
    </div>
  )
}
