import { api } from './client'
import type { Vaccination, Pregnancy, TbTest, Weight, Removal } from './health'

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
  created_at: string
  updated_at: string
}

export interface AnimalProfile extends Animal {
  vaccinations: Vaccination[]
  pregnancies: Pregnancy[]
  tb_tests: TbTest[]
  weights: Weight[]
  removal: Removal | null
}

export interface CreateAnimalPayload {
  tag_id: number
  breed: string
  category: string
  sex: string
  dob?: string
  notes?: string
}

export interface PatchAnimalPayload {
  breed?: string
  category?: string
  sex?: string
  dob?: string
  notes?: string
  is_active?: boolean
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
}

export const BREEDS = [
  'Angus', 'Shorthorn', 'Hereford', 'Holando Argentino',
  'Brangus', 'Brahman', 'Limangus', 'Crossbred', 'Other',
]

export const CATEGORIES = ['bull', 'cow', 'heifer', 'steer', 'male_calf', 'female_calf']
