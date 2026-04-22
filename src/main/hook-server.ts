import express from 'express'
import type { BrowserWindow } from 'electron'

const PORT = 7823

export function startHookServer(win: BrowserWindow): void {
  const app = express()

  app.use(express.json({ limit: '2mb' }))
  // Claude Code hooks pipe raw text — parse both
  app.use(express.text({ type: '*/*', limit: '2mb' }))

  function send(channel: string, body: unknown): void {
    const data = typeof body === 'string' ? tryParse(body) : body
    if (!win.isDestroyed()) {
      win.webContents.send(channel, data)
    }
  }

  // SessionStart hook
  app.post('/session', (req, res) => {
    send('island:session-start', req.body)
    res.sendStatus(200)
  })

  // PreToolUse hook — show tool being called
  app.post('/tool', (req, res) => {
    send('island:tool-active', req.body)
    res.sendStatus(200)
  })

  // Stop hook — final result with cost/turns
  app.post('/done', (req, res) => {
    send('island:task-done', req.body)
    res.sendStatus(200)
  })

  app.listen(PORT, '127.0.0.1', () => {
    console.log(`[hook-server] listening on http://127.0.0.1:${PORT}`)
  })
}

function tryParse(str: string): unknown {
  try {
    return JSON.parse(str)
  } catch {
    return { raw: str }
  }
}
