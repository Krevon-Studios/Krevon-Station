import { useState, useEffect, useRef, useCallback } from 'react'
import type { IslandState, MediaSessionData } from '../types'

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

export interface IslandStore {
  state: IslandState
  nextMedia: () => void
  prevMedia: () => void
}

export function useIslandStore(): IslandStore {
  const [state, setState] = useState<IslandState>({ mode: 'idle' })
  const timerRef  = useRef<ReturnType<typeof setTimeout>>()
  const mediaSessionsRef  = useRef<MediaSessionData[]>([])
  const mediaActiveIndexRef = useRef<number>(0)
  const claudeRef = useRef(false)

  const updateMediaState = useCallback(() => {
    const sessions = mediaSessionsRef.current
    if (sessions.length === 0) {
      if (!claudeRef.current) setState({ mode: 'idle' })
    } else {
      if (mediaActiveIndexRef.current >= sessions.length) mediaActiveIndexRef.current = 0
      if (mediaActiveIndexRef.current < 0) mediaActiveIndexRef.current = sessions.length - 1
      if (!claudeRef.current) {
        setState({
          mode: 'media',
          session: sessions[mediaActiveIndexRef.current],
          sessions,
          activeIndex: mediaActiveIndexRef.current
        })
      }
    }
  }, [])

  useEffect(() => {
    function goIdleOrMedia(ms: number) {
      clearTimeout(timerRef.current)
      timerRef.current = setTimeout(() => {
        claudeRef.current = false
        updateMediaState()
      }, ms)
    }

    const unsubStart = window.island.onSessionStart((raw) => {
      const d = raw as Record<string, unknown>
      clearTimeout(timerRef.current)
      claudeRef.current = true
      setState({ mode: 'session_start', sessionId: String(d.session_id ?? '') })
      goIdleOrMedia(2500)
    })

    const unsubTool = window.island.onToolActive((raw) => {
      const d = raw as Record<string, unknown>
      clearTimeout(timerRef.current)
      claudeRef.current = true
      const name = String(d.tool_name ?? 'tool')
      setState({ mode: 'tool_active', toolName: name, displayLabel: toolLabel(name) })
    })

    const unsubDone = window.island.onTaskDone((raw) => {
      const d = raw as Record<string, unknown>
      clearTimeout(timerRef.current)
      const result = (d.result ?? d) as Record<string, unknown>
      setState({
        mode: 'task_done',
        cost:       Number(result.total_cost_usd ?? 0),
        turns:      Number(result.num_turns      ?? 0),
        durationMs: Number(result.duration_ms    ?? 0)
      })
      goIdleOrMedia(5000)
    })

    const unsubMedia = window.island.onMedia((raw) => {
      const d = raw as { sessions?: unknown[] }
      const incoming = (d.sessions ?? []) as Array<Record<string, unknown>>

      const sessions: MediaSessionData[] = incoming
        .map((s) => ({
          title:       String(s.title       ?? ''),
          artist:      String(s.artist      ?? ''),
          thumbnail:   String(s.thumbnail   ?? ''),
          status:      (s.status === 'playing' ? 'playing' : 'paused') as 'playing' | 'paused',
          hasSkip:     s.hasSkip === true,
          source:      String(s.sourceAppId ?? ''),
          sourceAppId: String(s.sourceAppId ?? ''),
        }))
        .filter((s) => s.title) // only keep valid sessions

      const previousActiveSession = mediaSessionsRef.current[mediaActiveIndexRef.current]
      mediaSessionsRef.current = sessions

      if (previousActiveSession && sessions.length > 0) {
        const newIndex = sessions.findIndex(s => s.sourceAppId === previousActiveSession.sourceAppId)
        if (newIndex !== -1) {
          mediaActiveIndexRef.current = newIndex
        } else {
          mediaActiveIndexRef.current = 0
        }
      } else if (sessions.length === 0) {
        mediaActiveIndexRef.current = 0
      }

      updateMediaState()
    })

    return () => {
      clearTimeout(timerRef.current)
      unsubStart()
      unsubTool()
      unsubDone()
      unsubMedia()
    }
  }, [updateMediaState])

  return {
    state,
    nextMedia: () => {
      mediaActiveIndexRef.current++
      updateMediaState()
    },
    prevMedia: () => {
      mediaActiveIndexRef.current--
      updateMediaState()
    }
  }
}
