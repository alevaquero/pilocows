import { api } from './client'

// ---------------------------------------------------------------------------
// Types from the handheld BLE layer (mirrored from Rust structs)
// ---------------------------------------------------------------------------

export interface DeviceInfo {
  id: string
  name: string
  // Signal strength in dBm at scan time (e.g. -50 strong, -90 very weak).
  // null if the OS didn't report one for this advertisement.
  rssi: number | null
}

export interface HeldSession {
  id: number
  name: string
  session_type: string
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
  test_result?: string
  test_name?: string
  vaccines?: string
  notes?: string
  has_audio: boolean
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
  test_result?: string
  test_name?: string
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
