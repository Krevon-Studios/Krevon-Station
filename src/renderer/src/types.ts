export type IslandMode = 'idle' | 'session_start' | 'tool_active' | 'task_done' | 'media'

export interface MediaSessionData {
  title:       string
  artist:      string
  status:      'playing' | 'paused'
  source:      string   // display name, e.g. "Spotify"
  sourceAppId: string   // raw AUMID, e.g. "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"
}

export type IslandState =
  | { mode: 'idle' }
  | { mode: 'session_start'; sessionId: string }
  | { mode: 'tool_active'; toolName: string; displayLabel: string }
  | { mode: 'task_done'; cost: number; turns: number; durationMs: number }
  | { mode: 'media'; sessions: MediaSessionData[]; index: number }
