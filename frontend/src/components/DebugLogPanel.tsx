import { useRef, useEffect, useState } from 'react'
import { useDebugLog, clear, type LogEntry, type LogLevel } from '../debugLog'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function levelClass(level: LogLevel): string {
  switch (level) {
    case 'error': return 'text-red-400'
    case 'warn':  return 'text-amber-400'
    case 'debug': return 'text-slate-500'
    default:      return 'text-slate-200'
  }
}

function levelBadge(level: LogLevel): string {
  switch (level) {
    case 'error': return 'ERR '
    case 'warn':  return 'WARN'
    case 'debug': return 'DBG '
    default:      return 'INFO'
  }
}

function fmt(ts: Date): string {
  return ts.toLocaleTimeString('en-GB', { hour12: false }) +
    '.' + String(ts.getMilliseconds()).padStart(3, '0')
}

// ---------------------------------------------------------------------------
// Log row
// ---------------------------------------------------------------------------

function LogRow({ entry }: { entry: LogEntry }) {
  return (
    <div className="flex gap-2 font-mono text-xs leading-5 min-w-0 select-text cursor-text">
      <span className="text-slate-600 shrink-0">{fmt(entry.ts)}</span>
      <span className={`shrink-0 font-semibold ${levelClass(entry.level)}`}>
        {levelBadge(entry.level)}
      </span>
      <span className={`break-all ${levelClass(entry.level)}`}>{entry.message}</span>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

interface Props {
  onClose: () => void
}

export default function DebugLogPanel({ onClose }: Props) {
  const entries = useDebugLog()
  const [autoScroll, setAutoScroll] = useState(true)
  const [filter, setFilter] = useState<LogLevel | 'all'>('all')
  const bottomRef = useRef<HTMLDivElement>(null)
  const scrollRef = useRef<HTMLDivElement>(null)

  // Auto-scroll to bottom when new entries arrive
  useEffect(() => {
    if (autoScroll && bottomRef.current) {
      bottomRef.current.scrollIntoView({ behavior: 'instant' })
    }
  }, [entries, autoScroll])

  // If user scrolls up manually → disable auto-scroll
  const handleScroll = () => {
    const el = scrollRef.current
    if (!el) return
    const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 40
    if (!atBottom && autoScroll) setAutoScroll(false)
  }

  const displayed = filter === 'all' ? entries : entries.filter(e => e.level === filter)

  return (
    <div className="flex flex-col bg-slate-900 border-t border-slate-700" style={{ height: 260 }}>
      {/* Toolbar */}
      <div className="flex items-center gap-3 px-3 py-1.5 border-b border-slate-700 shrink-0">
        <span className="text-slate-400 text-xs font-semibold tracking-wide uppercase">
          Debug Log
        </span>

        {/* Level filter */}
        <div className="flex gap-1 ml-2">
          {(['all', 'info', 'debug', 'warn', 'error'] as const).map(lvl => (
            <button
              key={lvl}
              onClick={() => setFilter(lvl)}
              className={`text-xs px-2 py-0.5 rounded transition-colors ${
                filter === lvl
                  ? 'bg-slate-600 text-white'
                  : 'text-slate-500 hover:text-slate-300'
              }`}
            >
              {lvl}
            </button>
          ))}
        </div>

        <div className="flex-1" />

        {/* Entry count */}
        <span className="text-slate-600 text-xs">{displayed.length} entries</span>

        {/* Auto-scroll toggle */}
        <label className="flex items-center gap-1.5 text-xs text-slate-400 cursor-pointer select-none">
          <input
            type="checkbox"
            checked={autoScroll}
            onChange={e => {
              setAutoScroll(e.target.checked)
              if (e.target.checked && bottomRef.current) {
                bottomRef.current.scrollIntoView({ behavior: 'instant' })
              }
            }}
            className="accent-slate-400 w-3 h-3"
          />
          Auto-scroll
        </label>

        {/* Clear */}
        <button
          onClick={clear}
          className="text-xs text-slate-500 hover:text-slate-200 transition-colors px-2"
        >
          Clear
        </button>

        {/* Close */}
        <button
          onClick={onClose}
          className="text-slate-500 hover:text-slate-200 transition-colors text-base leading-none px-1"
          aria-label="Close debug log"
        >
          ✕
        </button>
      </div>

      {/* Log content */}
      <div
        ref={scrollRef}
        onScroll={handleScroll}
        className="flex-1 overflow-y-auto px-3 py-2 space-y-0.5"
      >
        {displayed.length === 0 ? (
          <p className="text-slate-600 text-xs font-mono italic">No entries yet.</p>
        ) : (
          displayed.map(e => <LogRow key={e.id} entry={e} />)
        )}
        <div ref={bottomRef} />
      </div>
    </div>
  )
}
