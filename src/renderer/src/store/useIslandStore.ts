import { useState, useEffect, useRef } from 'react'
import type { IslandState, MediaSessionData } from '../types'

// Window dimensions per state — 4px padding each side
const P = 8
export const STATE_SIZES: Record<IslandState['mode'], { w: number; h: number }> = {
  idle:          { w: 165 + P, h: 44 + P },
  session_start: { w: 215 + P, h: 54 + P },
  tool_active:   { w: 275 + P, h: 62 + P },
  task_done:     { w: 310 + P, h: 62 + P },
  media:         { w: 370 + P, h: 66 + P }
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
}

export function useIslandStore(): IslandStore {
  const [state, setState] = useState<IslandState>({ mode: 'idle' })
  const timerRef  = useRef<ReturnType<typeof setTimeout>>()
  const mediaRef  = useRef<MediaSessionData | null>(null)
  const claudeRef = useRef(false)

  useEffect(() => {
    function goIdleOrMedia(ms: number) {
      clearTimeout(timerRef.current)
      timerRef.current = setTimeout(() => {
        claudeRef.current = false
        if (mediaRef.current) {
          resize('media')
          setState({ mode: 'media', session: mediaRef.current })
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
        thumbnail:   String(s.thumbnail   ?? ''),
        status:      s.status === 'playing' ? 'playing' : 'paused',
        hasSkip:     s.hasSkip === true,
        source:      String(s.sourceAppId ?? ''),
        sourceAppId: String(s.sourceAppId ?? ''),
      }))

      // index 0 = Windows-active session (sorted in worker); only show if has title
      const current = sessions.find(s => s.title) ?? null
      mediaRef.current = current

      if (!claudeRef.current) {
        if (!current) {
          resize('idle')
          setState({ mode: 'idle' })
        } else {
          resize('media')
          setState({ mode: 'media', session: current })
        }
      }
    })

    return () => {
      clearTimeout(timerRef.current)
      window.island.removeAllListeners()
    }
  }, [])

  return { state }
}
