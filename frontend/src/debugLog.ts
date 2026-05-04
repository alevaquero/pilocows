// ---------------------------------------------------------------------------
// Debug log store — module-level singleton so any component can append to it
// without prop drilling or a heavy state library.
// ---------------------------------------------------------------------------

import { useState, useEffect } from 'react'

export type LogLevel = 'info' | 'warn' | 'error' | 'debug'

export interface LogEntry {
  id: number
  ts: Date
  level: LogLevel
  message: string
}

type Subscriber = (entries: LogEntry[]) => void

const MAX_ENTRIES = 1000

let _entries: LogEntry[] = []
let _nextId = 0
const _subscribers = new Set<Subscriber>()

function _notify() {
  const snapshot = [..._entries]
  _subscribers.forEach(fn => fn(snapshot))
}

// ---------------------------------------------------------------------------
// Public write API
// ---------------------------------------------------------------------------

export function log(message: string, level: LogLevel = 'info'): void {
  _entries.push({ id: _nextId++, ts: new Date(), level, message })
  if (_entries.length > MAX_ENTRIES) {
    _entries = _entries.slice(-MAX_ENTRIES)
  }
  _notify()
}

export function dbg(message: string): void  { log(message, 'debug') }
export function warn(message: string): void  { log(message, 'warn')  }
export function err(message: string): void   { log(message, 'error') }

export function clear(): void {
  _entries = []
  _notify()
}

// ---------------------------------------------------------------------------
// React hook — subscribes the component to log changes
// ---------------------------------------------------------------------------

export function useDebugLog(): LogEntry[] {
  const [entries, setEntries] = useState<LogEntry[]>([..._entries])

  useEffect(() => {
    // sync with any entries written before mount
    setEntries([..._entries])
    _subscribers.add(setEntries)
    return () => { _subscribers.delete(setEntries) }
  }, [])

  return entries
}
