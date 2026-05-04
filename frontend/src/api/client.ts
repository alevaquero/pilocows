import { dbg, err as dErr } from '../debugLog'

const BASE = import.meta.env.VITE_API_URL ?? 'http://127.0.0.1:8742/api/v1'

export class ApiError extends Error {
  constructor(public status: number, message: string) {
    super(message)
    this.name = 'ApiError'
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const method = init?.method ?? 'GET'
  const url = `${BASE}${path}`

  // Log outgoing request (include body for POST/PATCH, truncated at 300 chars)
  if (init?.body) {
    const preview = String(init.body).slice(0, 300)
    const truncated = String(init.body).length > 300 ? '…' : ''
    dbg(`→ ${method} ${path}  body: ${preview}${truncated}`)
  } else {
    dbg(`→ ${method} ${path}`)
  }

  const res = await fetch(url, {
    headers: { 'Content-Type': 'application/json', ...init?.headers },
    ...init,
  })

  if (!res.ok) {
    const body = await res.json().catch(() => ({ error: res.statusText }))
    const msg = body?.error ?? res.statusText
    dErr(`← ${method} ${path}  ${res.status} ${msg}`)
    throw new ApiError(res.status, msg)
  }

  // 204 No Content — return undefined cast to T
  if (res.status === 204) {
    dbg(`← ${method} ${path}  204`)
    return undefined as T
  }

  const data = await res.json() as T
  // Log response summary — stringify and truncate at 300 chars
  const summary = JSON.stringify(data)
  const truncated = summary.length > 300 ? summary.slice(0, 300) + '…' : summary
  dbg(`← ${method} ${path}  ${res.status}  ${truncated}`)

  return data
}

export const api = {
  get: <T>(path: string) => request<T>(path),
  post: <T>(path: string, body: unknown) =>
    request<T>(path, { method: 'POST', body: JSON.stringify(body) }),
  patch: <T>(path: string, body: unknown) =>
    request<T>(path, { method: 'PATCH', body: JSON.stringify(body) }),
  delete: <T>(path: string) =>
    request<T>(path, { method: 'DELETE' }),
}
