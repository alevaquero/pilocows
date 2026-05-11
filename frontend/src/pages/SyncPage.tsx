import { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { invoke } from '@tauri-apps/api/core'
import {
  type DeviceInfo,
  type HeldSession,
  type SessionRecord,
  type SyncResponse,
  syncApi,
} from '../api/sync'
import {
  sessionsApi,
  type IncomingSession,
  type IncomingSessionRecord,
} from '../api/sessions'
import { tagsApi } from '../api/tags'
import * as dlog from '../debugLog'

// ---------------------------------------------------------------------------
// Helpers — convert BLE types to sessions/sync payload types
// ---------------------------------------------------------------------------

const SESSION_TYPE_INT: Record<string, number> = {
  general: 0, weighing: 1, vaccination: 2, pregnancy: 3, tb_test: 4,
}

function bleRecordToIncoming(r: SessionRecord): IncomingSessionRecord {
  const ts = Math.floor(new Date(r.scanned_at).getTime() / 1000)
  return {
    eid: r.eid,
    ts,
    type: SESSION_TYPE_INT[r.event_type] ?? 0,
    weight_kg: r.weight_kg,
    pregnancy: r.pregnancy_result,
    tb_result: r.tb_result,
    vaccines: r.vaccines,
    note: r.notes,
  }
}

// ---------------------------------------------------------------------------
// Per-session sync state
// ---------------------------------------------------------------------------

type SessionSyncState =
  | { status: 'idle' }
  | { status: 'syncing' }
  | { status: 'done'; accepted: number; unregistered: UnregState[] }
  | { status: 'error'; message: string }

interface UnregState {
  eid: string
  reg: 'pending' | 'registering' | 'done' | 'error'
  error?: string
}

// ---------------------------------------------------------------------------
// Selectable error box
// ---------------------------------------------------------------------------

function ErrorBox({ message }: { message: string }) {
  return (
    <div className="bg-red-50 border border-red-200 rounded-xl p-4">
      <p className="text-sm text-red-700 select-text cursor-text break-all">{message}</p>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Page component
// ---------------------------------------------------------------------------

export default function SyncPage() {
  const { t } = useTranslation()

  // ── BLE scan / connect state
  const [scanning, setScanning] = useState(false)
  const [devices, setDevices] = useState<DeviceInfo[]>([])
  const [scanned, setScanned] = useState(false)

  const [connecting, setConnecting] = useState<string | null>(null)
  const [connectedDevice, setConnectedDevice] = useState<DeviceInfo | null>(null)

  // ── Session list state
  const [sessions, setSessions] = useState<HeldSession[]>([])
  const [loadingSessions, setLoadingSessions] = useState(false)

  // ── Per-session sync state  (keyed by session id)
  const [sessionSync, setSessionSync] = useState<Record<number, SessionSyncState>>({})

  // ── Delete confirmation
  const [deleteConfirm, setDeleteConfirm] = useState<number | null>(null)

  const [pageError, setPageError] = useState('')
  const [lostConnection, setLostConnection] = useState(false)

  // ── Ref so the interval callback always sees the latest connectedDevice
  const connectedDeviceRef = useRef(connectedDevice)
  useEffect(() => { connectedDeviceRef.current = connectedDevice }, [connectedDevice])

  // ── Connection health-check: poll every 5 s while a device is shown as connected
  useEffect(() => {
    const id = setInterval(async () => {
      if (!connectedDeviceRef.current) return
      try {
        const still = await invoke<boolean>('ble_check_connection')
        if (!still) {
          dlog.warn('Connection health-check: device no longer connected — resetting UI')
          setConnectedDevice(null)
          setSessions([])
          setSessionSync({})
          setLostConnection(true)
        }
      } catch {
        // ignore transient errors from the check itself
      }
    }, 5000)
    return () => clearInterval(id)
  }, []) // intentionally empty — interval is started once; uses ref for current device

  // ── Helpers
  const setSync = (id: number, s: SessionSyncState) =>
    setSessionSync(prev => ({ ...prev, [id]: s }))

  // ---------------------------------------------------------------------------
  // Scan
  // ---------------------------------------------------------------------------
  const handleScan = async () => {
    setScanning(true)
    setDevices([])
    setScanned(false)
    setPageError('')
    setLostConnection(false)
    dlog.log('BLE scan started (4-second window)…')
    try {
      const found = await invoke<DeviceInfo[]>('ble_scan')
      if (found.length === 0) {
        dlog.warn('Scan complete — no named BLE devices found')
      } else {
        dlog.log(`Scan complete — ${found.length} device(s) found:`)
        found.forEach((d, i) =>
          dlog.dbg(`  [${i + 1}] name="${d.name}"  id=${d.id}`)
        )
      }
      setDevices(found)
      setScanned(true)
    } catch (e) {
      dlog.err(`Scan error: ${String(e)}`)
      setPageError(String(e))
    } finally {
      setScanning(false)
    }
  }

  // ---------------------------------------------------------------------------
  // Connect
  // ---------------------------------------------------------------------------
  const handleConnect = async (device: DeviceInfo) => {
    setConnecting(device.id)
    setPageError('')
    dlog.log(`Connecting to "${device.name}"  (id=${device.id})…`)
    try {
      await invoke('ble_connect', { deviceId: device.id })
      dlog.log(`Connected — discovering services…`)
      setConnectedDevice(device)
      await loadSessions()
    } catch (e) {
      dlog.err(`Connect failed: ${String(e)}`)
      setPageError(String(e))
    } finally {
      setConnecting(null)
    }
  }

  // ---------------------------------------------------------------------------
  // Load sessions
  // ---------------------------------------------------------------------------
  const loadSessions = async () => {
    setLoadingSessions(true)
    dlog.log('Reading SESSION_LIST from handheld (paginated, 6 sessions/page)…')
    try {
      const list = await invoke<HeldSession[]>('ble_read_sessions')
      const synced   = list.filter(s => s.synced).length
      const unsynced = list.length - synced
      dlog.log(`SESSION_LIST received — ${list.length} session(s) total (${unsynced} unsynced, ${synced} already synced):`)
      list.forEach(s => {
        const date = new Date(s.ts * 1000).toLocaleDateString()
        dlog.dbg(
          `  id=${s.id}  name="${s.name}"  type=${s.session_type}` +
          `  count=${s.count}  date=${date}` +
          `  synced=${s.synced}`
        )
      })
      if (list.length === 0) {
        dlog.warn('No sessions found on the handheld.')
      }
      setSessions(list)
      setSessionSync({})
    } catch (e) {
      const msg = String(e)
      dlog.err(`SESSION_LIST read error: ${msg}`)
      if (msg.includes('parse error')) {
        dlog.warn('Tip: ensure the handheld firmware is up to date (pagination support required)')
      }
      setPageError(msg)
    } finally {
      setLoadingSessions(false)
    }
  }

  // ---------------------------------------------------------------------------
  // Disconnect
  // ---------------------------------------------------------------------------
  const handleDisconnect = async () => {
    dlog.log('Disconnecting from handheld…')
    try {
      await invoke('ble_disconnect')
      dlog.log('Disconnected.')
    } catch (e) {
      dlog.warn(`Disconnect error (ignored): ${String(e)}`)
    }
    setConnectedDevice(null)
    setSessions([])
    setSessionSync({})
    setDevices([])
    setScanned(false)
  }

  // ---------------------------------------------------------------------------
  // Sync one session
  // ---------------------------------------------------------------------------
  const handleSyncSession = async (session: HeldSession) => {
    setSync(session.id, { status: 'syncing' })
    dlog.log(`──────────────────────────────────────────`)
    dlog.log(`Syncing session ${session.id}: "${session.name}"`)

    try {
      // Step 1a — read SESSION_META
      dlog.log(`  [1/4] Reading SESSION_META for session ${session.id}…`)
      const meta = await invoke<{ id: number; device_id: string; name: string; session_type: number; created_at: number; tag_count: number; synced: boolean; note: string }>('ble_read_session_meta', {
        sessionId: session.id,
      })
      dlog.log(`  [1/4] SESSION_META: name="${meta.name}" type=${meta.session_type} device=${meta.device_id}`)

      // Step 1b — read SESSION_DATA records
      dlog.log(`  [2/4] Reading SESSION_DATA for session ${session.id}…`)
      const records = await invoke<SessionRecord[]>('ble_read_session_data', {
        sessionId: session.id,
      })
      dlog.log(`  [2/4] SESSION_DATA received — ${records.length} record(s)`)
      records.forEach((r, i) => {
        const parts = [
          `eid=${r.eid}`,
          `type=${r.event_type}`,
          `at=${r.scanned_at}`,
        ]
        if (r.weight_kg != null)        parts.push(`weight=${r.weight_kg}kg`)
        if (r.vaccines)                 parts.push(`vaccines="${r.vaccines}"`)
        if (r.pregnancy_result)         parts.push(`preg="${r.pregnancy_result}"`)
        if (r.tb_result)                parts.push(`tb="${r.tb_result}"`)
        if (r.notes)                    parts.push(`notes="${r.notes}"`)
        dlog.dbg(`    [${i + 1}] ${parts.join('  ')}`)
      })

      // Step 2 — POST to sessions/sync (persist session + records in DB)
      dlog.log(`  [3/4] Posting session + ${records.length} records to backend…`)
      const incomingSession: IncomingSession = {
        handheld_session_id: meta.id,
        device_id: meta.device_id,
        name: meta.name,
        type: meta.session_type,
        created_at: meta.created_at,
        tag_count: meta.tag_count,
        note: meta.note,
      }
      try {
        const sessResp = await sessionsApi.sync({
          session: incomingSession,
          records: records.map(bleRecordToIncoming),
        })
        dlog.log(`  [3/4] sessions/sync OK — db_id=${sessResp.session_id} upserted=${sessResp.upserted_records}`)
      } catch (apiErr) {
        dlog.err(`  [3/4] sessions/sync failed: ${String(apiErr)}`)
        throw apiErr
      }

      // Step 3 — also post to legacy scan endpoint (existing unregistered-EID flow)
      dlog.log(`  [3/4b] Posting to legacy /sync/scans…`)
      let resp: SyncResponse
      try {
        resp = await syncApi.postScans(records)
      } catch (apiErr) {
        dlog.err(`  [3/4b] Backend /sync/scans failed: ${String(apiErr)}`)
        throw apiErr
      }
      dlog.log(
        `  [3/4b] /sync/scans accepted ${resp.accepted} record(s)` +
        (resp.unregistered_eids.length > 0
          ? `,  ${resp.unregistered_eids.length} unregistered EID(s): ${resp.unregistered_eids.join(', ')}`
          : '')
      )

      // Step 4 — mark synced on handheld
      dlog.log(`  [4/4] Marking session ${session.id} as synced on handheld…`)
      await invoke('ble_mark_session_synced', { sessionId: session.id })
      dlog.log(`  [4/4] Done.`)
      dlog.log(`Session ${session.id} sync complete.`)

      // Update synced flag in local list
      setSessions(prev =>
        prev.map(s => (s.id === session.id ? { ...s, synced: true } : s))
      )

      setSync(session.id, {
        status: 'done',
        accepted: resp.accepted,
        unregistered: resp.unregistered_eids.map(eid => ({
          eid,
          reg: 'pending' as const,
        })),
      })
    } catch (e) {
      const msg = String(e)
      dlog.err(`Session ${session.id} sync failed: ${msg}`)
      setSync(session.id, { status: 'error', message: msg })
      // Check if the failure was a lost connection and reset the UI if so
      try {
        const still = await invoke<boolean>('ble_check_connection')
        if (!still) {
          dlog.warn('BLE connection lost — resetting sync UI')
          setConnectedDevice(null)
          setSessions([])
          setSessionSync({})
          setLostConnection(true)
        }
      } catch { /* ignore */ }
    }
  }

  // ---------------------------------------------------------------------------
  // Register unregistered tag
  // ---------------------------------------------------------------------------
  const handleRegisterTag = async (sessionId: number, eid: string) => {
    dlog.log(`Registering tag EID=${eid} (from session ${sessionId})…`)
    setSessionSync(prev => {
      const s = prev[sessionId]
      if (s?.status !== 'done') return prev
      return {
        ...prev,
        [sessionId]: {
          ...s,
          unregistered: s.unregistered.map(u =>
            u.eid === eid ? { ...u, reg: 'registering' } : u
          ),
        },
      }
    })
    try {
      await tagsApi.create({
        tag_number: eid,
        purchased_at: new Date().toISOString().slice(0, 10),
        notes: 'Registered from BLE sync',
      })
      dlog.log(`Tag EID=${eid} registered OK.`)
      setSessionSync(prev => {
        const s = prev[sessionId]
        if (s?.status !== 'done') return prev
        return {
          ...prev,
          [sessionId]: {
            ...s,
            unregistered: s.unregistered.map(u =>
              u.eid === eid ? { ...u, reg: 'done' } : u
            ),
          },
        }
      })
    } catch (e) {
      dlog.err(`Tag EID=${eid} registration failed: ${String(e)}`)
      setSessionSync(prev => {
        const s = prev[sessionId]
        if (s?.status !== 'done') return prev
        return {
          ...prev,
          [sessionId]: {
            ...s,
            unregistered: s.unregistered.map(u =>
              u.eid === eid ? { ...u, reg: 'error', error: String(e) } : u
            ),
          },
        }
      })
    }
  }

  // ---------------------------------------------------------------------------
  // Delete session
  // ---------------------------------------------------------------------------
  const handleDelete = async (sessionId: number) => {
    setDeleteConfirm(null)
    dlog.log(`Deleting session ${sessionId} from handheld…`)
    try {
      await invoke('ble_delete_session', { sessionId })
      dlog.log(`Session ${sessionId} deleted.`)
      setSessions(prev => prev.filter(s => s.id !== sessionId))
      setSessionSync(prev => {
        const next = { ...prev }
        delete next[sessionId]
        return next
      })
    } catch (e) {
      dlog.err(`Delete session ${sessionId} failed: ${String(e)}`)
      setPageError(String(e))
    }
  }

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------
  return (
    <div className="p-8 max-w-3xl">
      <h2 className="text-xl font-semibold text-slate-800 mb-1">{t('sync.title')}</h2>
      <p className="text-slate-500 text-sm mb-8">{t('sync.description')}</p>

      {lostConnection && (
        <div className="mb-6 flex items-center justify-between bg-amber-50 border border-amber-200 rounded-xl px-4 py-3">
          <span className="text-sm text-amber-800">{t('sync.connection_lost')}</span>
          <button onClick={() => setLostConnection(false)} className="text-xs text-amber-600 hover:text-amber-800 font-medium ml-4">✕</button>
        </div>
      )}
      {pageError && <div className="mb-6"><ErrorBox message={pageError} /></div>}

      {/* ── Step 1: Connected header  OR  scan/device list ── */}
      {connectedDevice ? (
        <div className="flex items-center justify-between bg-green-50 border border-green-200 rounded-xl px-4 py-3 mb-6">
          <span className="text-sm text-green-800 font-medium">
            {t('sync.connected_to')} <span className="font-bold">{connectedDevice.name}</span>
          </span>
          <button
            onClick={handleDisconnect}
            className="text-xs text-red-600 hover:text-red-800 font-medium"
          >
            {t('sync.disconnect')}
          </button>
        </div>
      ) : (
        <div className="mb-6">
          <button
            onClick={handleScan}
            disabled={scanning}
            className="px-5 py-2.5 bg-slate-800 text-white rounded-xl text-sm font-medium hover:bg-slate-700 disabled:opacity-50 transition-colors"
          >
            {scanning ? t('sync.scanning') : t('sync.scan_devices')}
          </button>

          {scanned && devices.length === 0 && (
            <p className="mt-4 text-sm text-slate-500">{t('sync.no_devices')}</p>
          )}

          {devices.length > 0 && (
            <div className="mt-4 bg-white rounded-xl border border-slate-200 divide-y divide-slate-100 overflow-hidden">
              {devices.map(d => (
                <div key={d.id} className="flex items-center justify-between px-4 py-3">
                  <div>
                    <p className="text-sm font-medium text-slate-800">{d.name}</p>
                    <p className="text-xs text-slate-400 font-mono">{d.id}</p>
                  </div>
                  <button
                    onClick={() => handleConnect(d)}
                    disabled={connecting !== null}
                    className="px-3 py-1.5 text-xs rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50"
                  >
                    {connecting === d.id ? t('sync.connecting') : t('sync.connect')}
                  </button>
                </div>
              ))}
            </div>
          )}
        </div>
      )}

      {/* ── Step 2: Session list ── */}
      {connectedDevice && (
        <div>
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-sm font-semibold text-slate-600 uppercase tracking-wide">
              {t('sync.sessions_title')}
            </h3>
            <button
              onClick={loadSessions}
              disabled={loadingSessions}
              className="text-xs text-slate-500 hover:text-slate-800"
            >
              {loadingSessions ? t('common.loading') : '↻ Refresh'}
            </button>
          </div>

          {sessions.length === 0 && !loadingSessions && (
            <p className="text-sm text-slate-500">{t('sync.no_sessions')}</p>
          )}

          <div className="space-y-3">
            {sessions.map(session => {
              const syncState = sessionSync[session.id] ?? { status: 'idle' }
              return (
                <SessionCard
                  key={session.id}
                  session={session}
                  syncState={syncState}
                  onSync={() => handleSyncSession(session)}
                  onDelete={() => setDeleteConfirm(session.id)}
                  onRegisterTag={(eid) => handleRegisterTag(session.id, eid)}
                  t={t}
                />
              )
            })}
          </div>
        </div>
      )}

      {/* ── Delete confirmation dialog ── */}
      {deleteConfirm !== null && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
          <div className="bg-white rounded-2xl p-6 w-80 shadow-xl">
            <p className="text-sm text-slate-700 mb-6">{t('sync.delete_confirm')}</p>
            <div className="flex gap-3">
              <button
                onClick={() => setDeleteConfirm(null)}
                className="flex-1 py-2 text-sm rounded-xl border border-slate-200 text-slate-600 hover:bg-slate-50"
              >
                {t('common.cancel')}
              </button>
              <button
                onClick={() => handleDelete(deleteConfirm)}
                className="flex-1 py-2 text-sm rounded-xl bg-red-600 text-white hover:bg-red-700"
              >
                {t('sync.delete_session')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// SessionCard sub-component
// ---------------------------------------------------------------------------

interface SessionCardProps {
  session: HeldSession
  syncState: SessionSyncState
  onSync: () => void
  onDelete: () => void
  onRegisterTag: (eid: string) => void
  t: (key: string) => string
}

function SessionCard({ session, syncState, onSync, onDelete, onRegisterTag, t }: SessionCardProps) {
  const isSyncing = syncState.status === 'syncing'
  const isDone    = syncState.status === 'done'
  const isError   = syncState.status === 'error'

  const date = new Date(session.ts * 1000).toLocaleDateString()

  return (
    <div className="bg-white rounded-xl border border-slate-200 overflow-hidden">
      {/* Session header */}
      <div className="flex items-center justify-between px-4 py-3">
        <div>
          <div className="flex items-center gap-2">
            <span className="text-sm font-semibold text-slate-800">{session.name}</span>
            {(session.synced || isDone) && (
              <span className="text-xs px-2 py-0.5 rounded-full bg-blue-100 text-blue-700 font-medium">
                {t('sync.session_synced')}
              </span>
            )}
          </div>
          <p className="text-xs text-slate-400 mt-0.5">
            {session.session_type} · {session.count} {t('sync.scans_count')} · {date}
          </p>
        </div>

        <div className="flex items-center gap-2">
          {syncState.status === 'idle' || syncState.status === 'error' ? (
            <>
              <button
                onClick={onSync}
                className="px-3 py-1.5 text-xs rounded-lg bg-slate-800 text-white hover:bg-slate-700"
              >
                {t('sync.sync_session')}
              </button>
              <button
                onClick={onDelete}
                className="px-3 py-1.5 text-xs rounded-lg border border-red-200 text-red-600 hover:bg-red-50"
              >
                {t('sync.delete_session')}
              </button>
            </>
          ) : isSyncing ? (
            <span className="text-xs text-slate-400">{t('sync.syncing_session')}</span>
          ) : isDone ? (
            <button
              onClick={onDelete}
              className="px-3 py-1.5 text-xs rounded-lg border border-red-200 text-red-600 hover:bg-red-50"
            >
              {t('sync.delete_session')}
            </button>
          ) : null}
        </div>
      </div>

      {/* Error */}
      {isError && (
        <div className="px-4 pb-3">
          <p className="text-xs text-red-600 select-text cursor-text break-all">
            {(syncState as Extract<SessionSyncState, {status:'error'}>).message}
          </p>
        </div>
      )}

      {/* Sync result */}
      {isDone && (() => {
        const s = syncState as Extract<SessionSyncState, {status:'done'}>
        return (
          <div className="border-t border-slate-100 px-4 py-3 bg-slate-50">
            <p className="text-xs text-slate-500 mb-2">
              <span className="font-semibold text-slate-700">{s.accepted}</span> {t('sync.accepted')}
              {s.unregistered.length > 0 && (
                <> · <span className="font-semibold text-amber-600">{s.unregistered.length}</span> {t('sync.unregistered')}</>
              )}
            </p>
            {s.unregistered.length > 0 && (
              <div className="space-y-1">
                {s.unregistered.map(u => (
                  <div key={u.eid} className="flex items-center justify-between">
                    <span className="font-mono text-xs text-slate-700">{u.eid}</span>
                    {u.reg === 'pending' && (
                      <button
                        onClick={() => onRegisterTag(u.eid)}
                        className="text-xs px-2 py-1 rounded-lg bg-slate-800 text-white hover:bg-slate-700"
                      >
                        {t('sync.register_tag')}
                      </button>
                    )}
                    {u.reg === 'registering' && (
                      <span className="text-xs text-slate-400">{t('common.loading')}</span>
                    )}
                    {u.reg === 'done' && (
                      <span className="text-xs text-green-600 font-medium">{t('sync.registered_ok')}</span>
                    )}
                    {u.reg === 'error' && (
                      <span className="text-xs text-red-600 select-text cursor-text break-all">{u.error}</span>
                    )}
                  </div>
                ))}
              </div>
            )}
          </div>
        )
      })()}
    </div>
  )
}
