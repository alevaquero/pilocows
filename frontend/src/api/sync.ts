import { api } from './client'

// ---------------------------------------------------------------------------
// Types from the handheld BLE layer (mirrored from Rust structs)
// ---------------------------------------------------------------------------

export interface DeviceInfo {
  id: string
  name: string
}

export interface HeldSession {
  id: number
  name: string
  session_type: string
  status: 'open' | 'closed'
  count: number
  ts: number
  synced: boolean
}

export interface SessionRecord {
  eid: string
  event_type: string
  scanned_at: string
  session_id: number
  weight_kg?: number
  pregnancy_result?: string
  tb_result?: string
  vaccines?: string
  notes?: string
}

// ---------------------------------------------------------------------------
// Backend POST /sync/scans types
// ---------------------------------------------------------------------------

export interface IncomingScan {
  eid: string
  event_type: string
  scanned_at: string
  session_id?: number
  weight_kg?: number
  pregnancy_result?: string
  tb_result?: string
  vaccines?: string
  notes?: string
}

export interface SyncResponse {
  accepted: number
  unregistered_eids: string[]
}

// ---------------------------------------------------------------------------
// API calls
// ---------------------------------------------------------------------------

export const syncApi = {
  postScans: (scans: IncomingScan[]) =>
    api.post<SyncResponse>('/sync/scans', scans),
  listScans: () => api.get<unknown[]>('/sync/scans'),
}
