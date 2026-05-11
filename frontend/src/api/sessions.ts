import { api } from './client'

// ---------------------------------------------------------------------------
// Backend response types (mirroring Rust models)
// ---------------------------------------------------------------------------

export interface SessionSummary {
  id: number
  handheld_session_id: number
  device_id: string
  name: string
  session_type: number   // 0=general 1=weighing 2=vaccination 3=pregnancy 4=tb_test
  created_at: string
  tag_count: number
  handheld_note: string
  farm: string
  operator: string
  comments: string
  synced_at: string
  record_count: number
}

export interface DbSessionRecord {
  id: number
  session_id: number
  eid: string
  scanned_at: string
  event_data: string   // JSON blob: {"weight_kg":350} | {"vaccines":"..."} | etc.
  note: string
  tag_registered: boolean
  animal_id?: number
}

/** Single session row — no records, no record_count. Returned by PATCH. */
export type Session = Omit<SessionSummary, 'record_count'>

export interface SessionDetail extends Session {
  records: DbSessionRecord[]
}

export interface PatchSession {
  farm?: string
  operator?: string
  comments?: string
  handheld_note?: string
}

// ---------------------------------------------------------------------------
// Sync payload types (sent from frontend → backend after BLE read)
// ---------------------------------------------------------------------------

export interface IncomingSession {
  handheld_session_id: number
  device_id: string
  name: string
  type: number         // session_type integer
  created_at: number   // Unix timestamp
  tag_count: number
  note: string
}

export interface IncomingSessionRecord {
  eid: string
  ts: number           // Unix timestamp
  type: number
  weight_kg?: number
  pregnancy?: string
  tb_result?: string
  vaccines?: string
  note?: string
}

export interface SyncSessionPayload {
  session: IncomingSession
  records: IncomingSessionRecord[]
}

export interface SyncSessionResponse {
  session_id: number
  upserted_records: number
}

// ---------------------------------------------------------------------------
// API calls
// ---------------------------------------------------------------------------

export const sessionsApi = {
  list: () => api.get<SessionSummary[]>('/sessions'),
  get: (id: number) => api.get<SessionDetail>(`/sessions/${id}`),
  patch: (id: number, body: PatchSession) =>
    api.patch<Session>(`/sessions/${id}`, body),
  delete: (id: number) => api.delete<void>(`/sessions/${id}`),
  sync: (payload: SyncSessionPayload) =>
    api.post<SyncSessionResponse>('/sessions/sync', payload),
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const SESSION_TYPE_LABELS: Record<number, string> = {
  0: 'general',
  1: 'weighing',
  2: 'vaccination',
  3: 'pregnancy',
  4: 'tb_test',
}

export function sessionTypeKey(t: number): string {
  return SESSION_TYPE_LABELS[t] ?? 'general'
}

/** Parse event_data JSON blob to a human-readable summary string. */
export function formatEventData(
  eventData: string,
  sessionType: number,
  t?: (key: string) => string,
): string {
  try {
    const d = JSON.parse(eventData)
    switch (sessionType) {
      case 1: return d.weight_kg != null ? `${d.weight_kg} kg` : ''
      case 2: return d.vaccines ?? ''
      case 3: {
        const raw: string = d.pregnancy ?? ''
        return raw && t ? t(`pregnancy.${raw}`) : raw
      }
      case 4: {
        const raw: string = d.tb_result ?? ''
        return raw && t ? t(`tb_test.${raw}`) : raw
      }
      default: return ''
    }
  } catch {
    return ''
  }
}
