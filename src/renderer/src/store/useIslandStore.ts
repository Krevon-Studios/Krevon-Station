import { useState, useEffect, useRef, useCallback } from 'react'
import type { IslandState, MediaSessionData } from '../types'

// Window dimensions per state — 4px padding each side
const P = 8
export const STATE_SIZES: Record<IslandState['mode'], { w: number; h: number }> = {
  idle:          { w: 165 + P, h: 44 + P },
  session_start: { w: 215 + P, h: 54 + P },
  tool_active:   { w: 275 + P, h: 62 + P },
  task_done:     { w: 310 + P, h: 62 + P },
  media:         { w: 360 + P, h: 66 + P }
}

const TOOL_LABELS: Record<string, string> = {
  read:       'Reading file',
  write:      'Writing file',
  edit:       'Editing file',
  bash:       'Running command',
  glob:       'Searching files',
  grep:       'Searching code',
  web_search: 'Searching web',
  web_fetch:  'Fetching URL',
  todo:       'Updating tasks',
  agent:      'Spawning agent'
}

function toolLabel(name: string): string {
  const lower = name.toLowerCase()
  for (const [key, label] of Object.entries(TOOL_LABELS)) {
    if (lower.includes(key)) return label
  }
  return name.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase())
}

function resize(mode: IslandState['mode']) {
  const s = STATE_SIZES[mode]
  window.island.setWindowSize(s.w, s.h)
}

export interface IslandStore {
  state: IslandState
  selectMediaSource: (idx: number) => void
}

export function useIslandStore(): IslandStore {
  const [state, setState] = useState<IslandState>({ mode: 'idle' })
  const timerRef  = useRef<ReturnType<typeof setTimeout>>()
  const mediaRef  = useRef<MediaSessionData[] | null>(null)
  const claudeRef = useRef(false)
  // track selected index separately — survives SMTC updates
  const mediaIdxRef = useRef(0)

  const selectMediaSource = useCallback((idx: number) => {
    setState(prev => {
      if (prev.mode !== 'media') return prev
      const clamped = Math.max(0, Math.min(idx, prev.sessions.length - 1))
      if (clamped === prev.index) return prev
      return { ...prev, index: clamped }
    })
    mediaIdxRef.current = idx
  }, [])

  useEffect(() => {
    function goIdleOrMedia(ms: number) {
      clearTimeout(timerRef.current)
      timerRef.current = setTimeout(() => {
        claudeRef.current = false
        if (mediaRef.current?.length) {
          resize('media')
          setState({ mode: 'media', sessions: mediaRef.current, index: 0 })
          mediaIdxRef.current = 0
        } else {
          resize('idle')
          setState({ mode: 'idle' })
        }
      }, ms)
    }

    window.island.onSessionStart((raw) => {
      const d = raw as Record<string, unknown>
      clearTimeout(timerRef.current)
      claudeRef.current = true
      resize('session_start')
      setState({ mode: 'session_start', sessionId: String(d.session_id ?? '') })
      goIdleOrMedia(2500)
    })

    window.island.onToolActive((raw) => {
      const d = raw as Record<string, unknown>
      clearTimeout(timerRef.current)
      claudeRef.current = true
      const name = String(d.tool_name ?? 'tool')
      resize('tool_active')
      setState({ mode: 'tool_active', toolName: name, displayLabel: toolLabel(name) })
    })

    window.island.onTaskDone((raw) => {
      const d = raw as Record<string, unknown>
      clearTimeout(timerRef.current)
      const result = (d.result ?? d) as Record<string, unknown>
      resize('task_done')
      setState({
        mode: 'task_done',
        cost:       Number(result.total_cost_usd ?? 0),
        turns:      Number(result.num_turns      ?? 0),
        durationMs: Number(result.duration_ms    ?? 0)
      })
      goIdleOrMedia(5000)
    })

    window.island.onMedia((raw) => {
      const d = raw as { sessions?: unknown[] }
      const incoming = (d.sessions ?? []) as Array<Record<string, unknown>>

      const sessions: MediaSessionData[] = incoming.map((s) => ({
        title:       String(s.title       ?? ''),
        artist:      String(s.artist      ?? ''),
        status:      s.status === 'playing' ? 'playing' : 'paused',
        source:      String(s.sourceAppId ?? ''),
        sourceAppId: String(s.sourceAppId ?? ''),
      }))

      // only surfaces sessions that have a title
      const active = sessions.filter(s => s.title)
      mediaRef.current = active.length ? active : null

      if (!claudeRef.current) {
        if (!active.length) {
          resize('idle')
          setState({ mode: 'idle' })
          mediaIdxRef.current = 0
        } else {
          resize('media')
          setState(prev => {
            // preserve selected index if session still exists
            const prevIdx = prev.mode === 'media' ? prev.index : mediaIdxRef.current
            const idx = prevIdx < active.length ? prevIdx : 0
            mediaIdxRef.current = idx
            return { mode: 'media', sessions: active, index: idx }
          })
        }
      }
    })

    return () => {
      clearTimeout(timerRef.current)
      window.island.removeAllListeners()
    }
  }, [])

  return { state, selectMediaSource }
}
