import { api } from './client'

export interface HerdRow {
  id: number
  tag_number: string
  breed: string
  category: string
  sex: string
  dob: string | null
  is_active: number
  // Weight
  last_weight_kg: number | null
  last_weighed_at: string | null
  prev_weight_kg: number | null
  prev_weighed_at: string | null
  // Pregnancy
  last_pregnancy_result: string | null
  last_pregnancy_checked_at: string | null
  last_pregnancy_due_date: string | null
  // Test
  last_test_name: string | null
  last_test_result: string | null
  last_tested_at: string | null
  // Vaccination
  next_vaccine: string | null
  next_vaccine_due_at: string | null
  overdue_count: number
  // Removal
  removal_reason: string | null
  removal_date: string | null
}

export interface HerdReportParams {
  category?: string
  includeInactive?: boolean
}

export const reportsApi = {
  herd: (params?: HerdReportParams) => {
    const qs = new URLSearchParams()
    if (params?.category) qs.set('category', params.category)
    if (params?.includeInactive) qs.set('include_inactive', 'true')
    const q = qs.toString()
    return api.get<HerdRow[]>(`/reports/herd${q ? `?${q}` : ''}`)
  },
}

// ── Date helpers ──────────────────────────────────────────────────────────────

export function daysUntil(dateStr: string): number {
  const due = new Date(dateStr)
  const today = new Date()
  today.setHours(0, 0, 0, 0)
  due.setHours(0, 0, 0, 0)
  return Math.round((due.getTime() - today.getTime()) / 86_400_000)
}
