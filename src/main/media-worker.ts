/**
 * Worker thread: SMTC monitor isolated from Electron main thread.
 * Posts { sessions: MediaSessionData[] } on every change.
 */
import { parentPort } from 'worker_threads'

if (!parentPort) process.exit(1)

const PLAYING = 4

interface RawSession {
  sourceAppId?: string
  media?:       { title?: string; artist?: string } | null
  playback?:    { playbackStatus?: number }          | null
}

function toData(s: RawSession) {
  return {
    title:       s.media?.title  ?? '',
    artist:      s.media?.artist ?? '',
    status:      s.playback?.playbackStatus === PLAYING ? 'playing' : 'paused',
    sourceAppId: s.sourceAppId ?? '',
    source:      s.sourceAppId ?? '',  // display name mapped in renderer
  }
}

try {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const { SMTCMonitor } = require('@coooookies/windows-smtc-monitor') as {
    SMTCMonitor: {
      new(): { on(event: string, cb: (...a: unknown[]) => void): void; destroy(): void }
      getCurrentMediaSession(): RawSession | null
      getMediaSessions(): RawSession[]
    }
  }

  const sendAll = () => {
    const all = SMTCMonitor.getMediaSessions()
    parentPort!.postMessage({ sessions: all.map(toData) })
  }

  sendAll()

  const monitor = new SMTCMonitor()

  monitor.on('session-media-changed',    sendAll)
  monitor.on('session-playback-changed', sendAll)
  monitor.on('current-session-changed',  sendAll)
  monitor.on('session-removed',          sendAll)
  monitor.on('session-added',            sendAll)

  parentPort!.on('message', (msg) => {
    if (msg === 'destroy') { monitor.destroy(); process.exit(0) }
  })

  parentPort!.postMessage({ __status: 'ready' })
} catch (err) {
  parentPort!.postMessage({ __error: String(err) })
  process.exit(1)
}
