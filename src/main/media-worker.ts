/**
 * Worker thread: SMTC monitor isolated from Electron main thread.
 * Posts { sessions: MediaSessionData[] } on every change.
 */
import { parentPort } from 'worker_threads'

if (!parentPort) process.exit(1)

const PLAYING = 4

// Browsers report playbackType=Music even for video — detect by AUMID instead
const BROWSERS = ['chrome', 'firefox', 'msedge', 'edge', 'opera', 'brave', 'safari']
function isBrowser(appId: string): boolean {
  const s = appId.toLowerCase()
  return BROWSERS.some(b => s.includes(b))
}

interface RawSession {
  sourceAppId?: string
  media?:       { title?: string; artist?: string; thumbnail?: Buffer } | null
  playback?:    { playbackStatus?: number }                             | null
}

function thumbDataUrl(buf: Buffer | undefined | null): string {
  if (!buf || buf.length < 4) return ''
  const mime = (buf[0] === 0x89 && buf[1] === 0x50) ? 'image/png'
             : (buf[0] === 0xFF && buf[1] === 0xD8) ? 'image/jpeg'
             : 'image/jpeg'
  return `data:${mime};base64,${buf.toString('base64')}`
}

function toData(s: RawSession) {
  const appId = s.sourceAppId ?? ''
  return {
    title:       s.media?.title     ?? '',
    artist:      s.media?.artist    ?? '',
    thumbnail:   thumbDataUrl(s.media?.thumbnail),
    status:      s.playback?.playbackStatus === PLAYING ? 'playing' : 'paused',
    hasSkip:     !isBrowser(appId),
    sourceAppId: appId,
    source:      appId,
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
    const current = SMTCMonitor.getCurrentMediaSession()
    const currentId = current?.sourceAppId

    let sorted = all
    if (currentId) {
      const idx = all.findIndex(s => s.sourceAppId === currentId)
      if (idx > 0) sorted = [all[idx], ...all.slice(0, idx), ...all.slice(idx + 1)]
    }

    parentPort!.postMessage({ sessions: sorted.map(toData) })
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
