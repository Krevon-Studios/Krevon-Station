/**
 * NotificationCards.tsx
 *
 * Architecture mirrors the Drawer exactly:
 *  - The Electron window is a fixed 340×480 transparent overlay, NEVER moved or resized.
 *  - framer-motion AnimatePresence handles the slide-in / fade-out inside that overlay.
 *  - The panel's `top` CSS is driven by `onDrawerHeight` IPC (pure CSS, no window movement).
 *  - Fixed max-height scrollable card area with custom thin scrollbar + arrow indicator.
 */

import { AnimatePresence, motion } from 'framer-motion'
import { useEffect, useRef, useState, useCallback } from 'react'
import { X, Trash2, ChevronDown } from 'lucide-react'

// ── Motion constants — identical to Drawer ────────────────────────────────────
const SI = { type: 'spring', stiffness: 380, damping: 34, mass: 0.75 } as const
const CARD_EXIT = { opacity: 0, y: -6, transition: { duration: 0.12, ease: 'easeIn' as const } }

// ── Scrollable area max height ────────────────────────────────────────────────
const SCROLL_MAX_H = 320

// ── Types ─────────────────────────────────────────────────────────────────────

interface WindowsNotification {
  id: number
  appId: string
  appName: string
  title: string
  body: string
  addedAt: number
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function appColor(name: string): string {
  const palette = [
    '#7C6AFF', '#34D399', '#F97316', '#60A5FA',
    '#F472B6', '#A78BFA', '#22D3EE', '#4ADE80',
  ]
  let h = 0
  for (const c of name) h = (h * 31 + c.charCodeAt(0)) | 0
  return palette[Math.abs(h) % palette.length]
}

function formatTime(ts: number): string {
  return new Date(ts).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })
}

// ── App Icon cache (module-level — persists for the lifetime of the window) ───
// Main process already caches via _notifIconCache Map, but going through IPC
// on every card mount still costs a round-trip. This renderer-side cache makes
// every repeat lookup O(1) with zero IPC overhead.
const _iconCache = new Map<string, string | null>()
// Pending map deduplicates concurrent fetches for the same appId so we only
// fire one IPC call even if multiple cards mount simultaneously.
const _iconPending = new Map<string, Promise<string | null>>()

function fetchIcon(appId: string): Promise<string | null> {
  if (_iconCache.has(appId)) return Promise.resolve(_iconCache.get(appId) ?? null)
  if (_iconPending.has(appId)) return _iconPending.get(appId)!
  const p = window.island.getNotifIcon(appId)
    .then(url => { _iconCache.set(appId, url ?? null); return url ?? null })
    .catch(() => { _iconCache.set(appId, null); return null })
    .finally(() => _iconPending.delete(appId))
  _iconPending.set(appId, p)
  return p
}

// ── App Icon ──────────────────────────────────────────────────────────────────

function AppIcon({ appId, appName }: { appId: string; appName: string }) {
  const color  = appColor(appName || appId)
  const letter = (appName || appId || '?')[0].toUpperCase()
  // Seed from cache immediately — zero flash of letter avatar on repeat mounts
  const [icon, setIcon] = useState<string | null>(() => _iconCache.get(appId) ?? null)

  useEffect(() => {
    if (!appId) return
    if (_iconCache.has(appId)) {
      setIcon(_iconCache.get(appId) ?? null)
      return
    }
    let cancelled = false
    fetchIcon(appId).then(url => { if (!cancelled) setIcon(url) })
    return () => { cancelled = true }
  }, [appId])

  return (
    <div
      className="w-[38px] h-[38px] rounded-[11px] flex items-center justify-center shrink-0 overflow-hidden"
      style={{ background: icon ? 'transparent' : `${color}22` }}
    >
      {icon
        ? <img src={icon} className="w-full h-full object-cover rounded-[10px]" alt={appName} />
        : <span className="text-[15px] font-bold select-none" style={{ color }}>{letter}</span>
      }
    </div>
  )
}

// ── Single card ───────────────────────────────────────────────────────────────

function NotifCard({ n, onDismiss, onClick }: {
  n: WindowsNotification
  onDismiss(): void
  onClick(): void
}) {
  return (
    <motion.div
      initial={{ opacity: 0, y: -10 }}
      animate={{ opacity: 1, y: 0, transition: SI }}
      exit={CARD_EXIT}
      className="overflow-hidden cursor-pointer group/card shrink-0"
      style={{ background: '#000000', borderRadius: '14px' }}
    >
      <div
        className="flex items-start gap-[11px] px-[12px] pt-[11px] pb-[11px] hover:bg-white/[0.045] transition-colors duration-150"
        onClick={onClick}
      >
        <AppIcon appId={n.appId} appName={n.appName} />

        <div className="flex-1 min-w-0 pt-[1px]">
          <div className="flex items-center justify-between gap-2 mb-[3px]">
            <span
              className="text-[10.5px] font-semibold leading-none tracking-wide truncate"
              style={{ color: 'rgba(var(--accent-soft-rgb), 0.90)' }}
            >
              {n.appName || 'Notification'}
            </span>
            <div className="flex items-center gap-[6px] shrink-0">
              <span className="text-[9.5px] leading-none" style={{ color: 'rgba(255,255,255,0.30)' }}>
                {formatTime(n.addedAt)}
              </span>
              <button
                onClick={e => { e.stopPropagation(); onDismiss() }}
                className="flex items-center justify-center w-[20px] h-[20px] rounded-full transition-all bg-white/5 opacity-0 group-hover/card:opacity-100 hover:bg-white/15 active:bg-white/10 cursor-pointer"
                style={{ color: 'rgba(255,255,255,0.4)' }}
                onMouseEnter={e => (e.currentTarget.style.color = 'rgba(255,255,255,0.9)')}
                onMouseLeave={e => (e.currentTarget.style.color = 'rgba(255,255,255,0.4)')}
              >
                <X size={10} strokeWidth={2.5} />
              </button>
            </div>
          </div>

          {n.title && (
            <p className="text-[12.5px] font-semibold leading-snug truncate" style={{ color: 'rgba(255,255,255,0.90)' }}>
              {n.title}
            </p>
          )}
          {n.body && (
            <p className="text-[11px] leading-relaxed mt-[2px] line-clamp-2" style={{ color: 'rgba(255,255,255,0.42)' }}>
              {n.body}
            </p>
          )}
        </div>
      </div>
    </motion.div>
  )
}

// ── Scrollable area with overflow indicator ───────────────────────────────────

function ScrollArea({ children }: { children: React.ReactNode }) {
  const ref = useRef<HTMLDivElement>(null)
  const [canScrollDown, setCanScrollDown] = useState(false)

  const check = useCallback(() => {
    const el = ref.current
    if (!el) return
    setCanScrollDown(el.scrollHeight - el.scrollTop - el.clientHeight > 8)
  }, [])

  useEffect(() => {
    const el = ref.current
    if (!el) return
    check()
    const ro = new ResizeObserver(check)
    ro.observe(el)
    el.addEventListener('scroll', check, { passive: true })
    return () => { ro.disconnect(); el.removeEventListener('scroll', check) }
  }, [check])

  return (
    <div className="relative">
      <style>{`
        .notif-scroll::-webkit-scrollbar { width: 3px; }
        .notif-scroll::-webkit-scrollbar-track { background: transparent; }
        .notif-scroll::-webkit-scrollbar-thumb {
          background: rgba(255,255,255,0.12);
          border-radius: 99px;
          transition: background 0.15s;
        }
        .notif-scroll::-webkit-scrollbar-thumb:hover { background: rgba(255,255,255,0.24); }
      `}</style>

      <div
        ref={ref}
        className="notif-scroll flex flex-col gap-[4px] overflow-y-auto"
        style={{ maxHeight: SCROLL_MAX_H }}
      >
        {children}
      </div>

      {/* Scroll-more indicator — fades in when content overflows */}
      <AnimatePresence>
        {canScrollDown && (
          <motion.div
            key="scroll-hint"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.2 }}
            className="pointer-events-none absolute bottom-0 left-0 right-0 flex justify-center pb-[5px] pt-[20px]"
            style={{
              background: 'linear-gradient(to bottom, transparent, rgba(0,0,0,0.88) 55%)',
              borderBottomLeftRadius: '12px',
              borderBottomRightRadius: '12px',
            }}
          >
            <motion.div
              animate={{ y: [0, 3, 0] }}
              transition={{ repeat: Infinity, duration: 1.5, ease: 'easeInOut' }}
            >
              <ChevronDown size={13} strokeWidth={2.5} color="rgba(255,255,255,0.35)" />
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}

// ── Root ──────────────────────────────────────────────────────────────────────

export function NotificationCards() {
  const [notifs, setNotifs] = useState<WindowsNotification[]>([])
  const [visible, setVisible] = useState(false)
  // CSS top offset driven by drawer card height — zero Electron window movement
  const [panelTop, setPanelTop] = useState(60)


  // ── Drawer open/close — controls panel visibility ─────────────────────────
  useEffect(() => {
    const unsubShow        = window.island.onDrawerShow(() => setVisible(true))
    const unsubForceClose  = window.island.onDrawerForceClose(() => setVisible(false))
    const unsubClosed      = window.island.onDrawerClosed(() => setVisible(false))
    return () => { unsubShow(); unsubForceClose(); unsubClosed() }
  }, [])

  // ── Drawer height updates — positions panel immediately below card via CSS ─
  useEffect(() => {
    const unsub = window.island.onDrawerHeight((h: number) => {
      // +6px gap between drawer card bottom and notif panel top
      setPanelTop(h + 6)
    })
    return unsub
  }, [])

  // ── Notification events ───────────────────────────────────────────────────
  useEffect(() => {
    const unsub = window.island.onNotifications((raw: unknown) => {
      const data = raw as {
        type: string; id?: number; appId?: string; appName?: string
        title?: string; body?: string
      }
      if (data.type === 'added') {
        const n = data as Required<typeof data>
        // Pre-warm icon cache as soon as the notification arrives, before the
        // card even mounts — eliminates the icon-loading lag on first display
        if (n.appId) fetchIcon(n.appId)
        setNotifs(prev => {
          if (prev.some(x => x.id === n.id)) return prev
          return [
            { id: n.id, appId: n.appId, appName: n.appName, title: n.title, body: n.body,
              addedAt: (n as any).arrivalTime || Date.now() },
            ...prev,
          ].slice(0, 12)
        })
      } else if (data.type === 'removed') {
        setNotifs(prev => prev.filter(x => x.id !== data.id))
      }
    })
    return unsub
  }, [])

  const dismiss = useCallback((id: number) => {
    setNotifs(prev => prev.filter(n => n.id !== id))
    // Precisely remove this one notification from the Action Center via WinRT
    window.island.dismissNotifications([id])
  }, [])

  const clearAll = useCallback(() => {
    const ids = notifs.map(n => n.id)
    if (ids.length > 0) window.island.dismissNotifications(ids)
    setNotifs([])
  }, [notifs])

  const launchApp = useCallback((id: number, appId: string) => {
    if (appId) window.island.systemAction(`launch:${appId}`)
    dismiss(id)
  }, [dismiss])

  // Panel shows only when drawer is open AND there are notifications
  const showPanel = visible && notifs.length > 0



  return (
    // Full transparent overlay — identical structure to Drawer root
    <div className="w-full h-full relative pointer-events-none select-none">
      {/* Click-capture overlay covering everything below the drawer */}
      {visible && (
        <div 
          className="absolute left-0 right-0 bottom-0 pointer-events-auto"
          style={{ top: panelTop }}
          onClick={() => window.island.requestCloseDrawer()}
        />
      )}
      <AnimatePresence>
        {showPanel && (
          <motion.div
            key="notif-panel"
            className="absolute right-[8px] w-[320px] pointer-events-auto overflow-hidden"
            onClick={(e) => e.stopPropagation()}
            style={{
              top: panelTop,
              background: '#000000',
              borderRadius: '18px',
            }}
            initial={{ opacity: 0, y: -12 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -8, transition: { duration: 0.15, ease: 'easeIn' } }}
            transition={SI}
          >
            {/* Top inner sheen — matches Drawer */}
            <div className="absolute top-0 left-0 right-0 h-[1px] rounded-t-[18px]
                bg-gradient-to-r from-transparent via-white/10 to-transparent pointer-events-none z-10" />

            {/* Header */}
            <div
              className="flex items-center justify-between select-none px-[14px] shrink-0"
              style={{ height: '44px', borderBottom: '1px solid rgba(255,255,255,0.06)' }}
            >
              <span className="text-[12px] font-semibold" style={{ color: 'rgba(255,255,255,0.60)' }}>
                Notifications
              </span>
              <motion.button
                initial={{ opacity: 0, scale: 0.9 }}
                animate={{ opacity: 1, scale: 1 }}
                transition={{ duration: 0.15 }}
                onClick={clearAll}
                className="flex items-center gap-[6px] px-[8px] py-[4px] rounded-full transition-all cursor-pointer select-none"
                style={{ background: 'rgba(255,255,255,0.06)', color: 'rgba(255,255,255,0.5)' }}
                onMouseEnter={e => { e.currentTarget.style.background = 'rgba(255,255,255,0.12)'; e.currentTarget.style.color = 'rgba(255,255,255,0.85)' }}
                onMouseLeave={e => { e.currentTarget.style.background = 'rgba(255,255,255,0.06)'; e.currentTarget.style.color = 'rgba(255,255,255,0.5)' }}
                onMouseDown={e => { e.currentTarget.style.background = 'rgba(255,255,255,0.04)' }}
                onMouseUp={e => { e.currentTarget.style.background = 'rgba(255,255,255,0.12)' }}
              >
                <Trash2 size={11} strokeWidth={2} />
                <span className="text-[10px] font-medium leading-none pb-[0.5px]">Clear all</span>
              </motion.button>
            </div>

            {/* Scrollable card list */}
            <div className="p-[8px]">
              <ScrollArea>
                <AnimatePresence initial={false}>
                  {notifs.map(n => (
                    <NotifCard
                      key={n.id}
                      n={n}
                      onDismiss={() => dismiss(n.id)}
                      onClick={() => launchApp(n.id, n.appId)}
                    />
                  ))}
                </AnimatePresence>
              </ScrollArea>
            </div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}
