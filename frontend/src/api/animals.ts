import { api } from './client'
import type { Vaccination, Pregnancy, Test, Weight, Removal } from './health'

export interface Animal {
  id: number
  tag_id: number
  tag_number: string
  breed: string
  category: string
  sex: string
  dob: string | null
  notes: string
  is_active: number
  father_id: number | null
  mother_id: number | null
  father_tag_number: string | null
  mother_tag_number: string | null
  created_at: string
  updated_at: string
}

// A tag scan from a "General" (no structured health data) session — read-only,
// only ever produced by a handheld sync. See backend's GeneralScan model.
export interface GeneralScan {
  session_id: number
  session_name: string
  eid: string
  scanned_at: string
  note: string
  has_audio: boolean
}

export interface AnimalProfile extends Animal {
  vaccinations: Vaccination[]
  pregnancies: Pregnancy[]
  tests: Test[]
  weights: Weight[]
  removal: Removal | null
  general_scans: GeneralScan[]
}

export interface CreateAnimalPayload {
  tag_id: number
  breed: string
  category: string
  sex: string
  dob?: string
  notes?: string
  father_eid?: string
  mother_eid?: string
}

export interface PatchAnimalPayload {
  breed?: string
  category?: string
  sex?: string
  dob?: string
  notes?: string
  is_active?: boolean
  // Omit to leave unchanged, "" to clear, or an existing animal's EID to set.
  father_eid?: string
  mother_eid?: string
}

export interface AnimalQuery {
  tag_number?: string
  category?: string
  is_active?: boolean
}

export const animalsApi = {
  list: (q?: AnimalQuery) => {
    const params = new URLSearchParams()
    if (q?.tag_number) params.set('tag_number', q.tag_number)
    if (q?.category) params.set('category', q.category)
    if (q?.is_active !== undefined) params.set('is_active', String(q.is_active))
    const qs = params.toString()
    return api.get<Animal[]>(`/animals${qs ? `?${qs}` : ''}`)
  },
  get: (id: number) => api.get<AnimalProfile>(`/animals/${id}`),
  create: (payload: CreateAnimalPayload) => api.post<Animal>('/animals', payload),
  patch: (id: number, payload: PatchAnimalPayload) =>
    api.patch<Animal>(`/animals/${id}`, payload),
  delete: (id: number) =>
    api.delete<void>(`/animals/${id}`),
}

export const BREEDS = [
  'Angus Red', 'Angus Black', 'Shorthorn', 'Hereford', 'Holando Argentino',
  'Brangus', 'Brahman', 'Charolais', 'Limangus', 'Crossbred', 'Other',
]

// Breed values are stored and matched on as-is (English) and normally just
// displayed literally in both languages (e.g. "Holando Argentino",
// "Shorthorn"). A couple of breeds have a genuinely different Spanish name,
// so those route through i18n for display; everything else falls straight
// through unchanged. The stored/matched value never changes with locale.
const BREED_I18N_KEYS: Record<string, string> = {
  'Angus Red': 'animals.breed_angus_red',
  'Angus Black': 'animals.breed_angus_black',
}

export function breedLabel(t: (key: string) => string, breed: string): string {
  const key = BREED_I18N_KEYS[breed]
  return key ? t(key) : breed
}

export const CATEGORIES = ['bull', 'cow', 'heifer', 'steer', 'male_calf', 'female_calf']
