import { dbg, err as dErr } from '../debugLog'

const BASE = import.meta.env.VITE_API_URL ?? 'http://127.0.0.1:8742/api/v1'

export interface BackupBlob {
  filename: string
  data: number[]  // Uint8Array as plain array for Tauri invoke serialization
}

export const backupApi = {
  async fetch(): Promise<BackupBlob> {
    dbg('→ GET /backup')
    const res = await fetch(`${BASE}/backup`)
    if (!res.ok) {
      dErr(`← GET /backup  ${res.status} ${res.statusText}`)
      throw new Error(`Backup failed: ${res.statusText}`)
    }

    const disposition = res.headers.get('content-disposition') ?? ''
    const match = disposition.match(/filename="([^"]+)"/)
    const filename = match?.[1] ?? 'pilocows_backup.db'

    const arrayBuffer = await res.arrayBuffer()
    const data = Array.from(new Uint8Array(arrayBuffer))
    dbg(`← GET /backup  200  ${filename}  ${data.length} bytes`)
    return { filename, data }
  },

  async restore(file: File): Promise<{ status: string; message: string }> {
    dbg(`→ POST /backup  file=${file.name}  size=${file.size}`)
    const formData = new FormData()
    formData.append('file', file)

    const res = await fetch(`${BASE}/backup`, {
      method: 'POST',
      body: formData,
    })

    if (!res.ok) {
      const body = await res.json().catch(() => ({ error: res.statusText }))
      const msg = body?.error ?? res.statusText
      dErr(`← POST /backup  ${res.status} ${msg}`)
      throw new Error(msg)
    }

    const data = await res.json()
    dbg(`← POST /backup  ${res.status}  ${JSON.stringify(data)}`)
    return data
  },
}
