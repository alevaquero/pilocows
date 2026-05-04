import { api } from './client'

// ── Vaccinations ────────────────────────────────────────────────────────────

export interface Vaccination {
  id: number
  animal_id: number
  vaccine: string
  dose: string
  administered_at: string
  next_due_at: string | null
  notes: string
  created_at: string
}

export interface CreateVaccinationPayload {
  vaccine: string
  dose?: string
  administered_at: string
  next_due_at?: string
  notes?: string
}

// ── Pregnancies ──────────────────────────────────────────────────────────────

export interface Pregnancy {
  id: number
  animal_id: number
  result: string
  checked_at: string
  due_date: string | null
  notes: string
  created_at: string
}

export interface CreatePregnancyPayload {
  result: string
  checked_at: string
  due_date?: string
  notes?: string
}

// ── TB Tests ─────────────────────────────────────────────────────────────────

export interface TbTest {
  id: number
  animal_id: number
  result: string
  tested_at: string
  notes: string
  created_at: string
}

export interface CreateTbTestPayload {
  result: string
  tested_at: string
  notes?: string
}

// ── Weights ───────────────────────────────────────────────────────────────────

export interface Weight {
  id: number
  animal_id: number
  weight_kg: number
  weighed_at: string
  notes: string
  created_at: string
}

export interface CreateWeightPayload {
  weight_kg: number
  weighed_at: string
  notes?: string
}

// ── Removal ───────────────────────────────────────────────────────────────────

export interface Removal {
  id: number
  animal_id: number
  reason: string
  removed_at: string
  notes: string
  created_at: string
}

export interface CreateRemovalPayload {
  reason: string
  removed_at: string
  notes?: string
}

export interface PatchRemovalPayload {
  reason?: string
  removed_at?: string
  notes?: string
}

// ── API ────────────────────────────────────────────────────────────────────────

// ── Patch payloads ────────────────────────────────────────────────────────────

export interface PatchVaccinationPayload {
  vaccine?: string
  dose?: string
  administered_at?: string
  next_due_at?: string | null
  notes?: string
}

export interface PatchPregnancyPayload {
  result?: string
  checked_at?: string
  due_date?: string | null
  notes?: string
}

export interface PatchTbTestPayload {
  result?: string
  tested_at?: string
  notes?: string
}

export interface PatchWeightPayload {
  weight_kg?: number
  weighed_at?: string
  notes?: string
}

// ── API ────────────────────────────────────────────────────────────────────────

export const healthApi = {
  // Vaccinations
  listVaccinations: (animal_id: number) =>
    api.get<Vaccination[]>(`/animals/${animal_id}/vaccinations`),
  createVaccination: (animal_id: number, p: CreateVaccinationPayload) =>
    api.post<Vaccination>(`/animals/${animal_id}/vaccinations`, p),
  patchVaccination: (animal_id: number, id: number, p: PatchVaccinationPayload) =>
    api.patch<Vaccination>(`/animals/${animal_id}/vaccinations/${id}`, p),
  deleteVaccination: (animal_id: number, id: number) =>
    api.delete<void>(`/animals/${animal_id}/vaccinations/${id}`),

  // Pregnancies
  listPregnancies: (animal_id: number) =>
    api.get<Pregnancy[]>(`/animals/${animal_id}/pregnancies`),
  createPregnancy: (animal_id: number, p: CreatePregnancyPayload) =>
    api.post<Pregnancy>(`/animals/${animal_id}/pregnancies`, p),
  patchPregnancy: (animal_id: number, id: number, p: PatchPregnancyPayload) =>
    api.patch<Pregnancy>(`/animals/${animal_id}/pregnancies/${id}`, p),
  deletePregnancy: (animal_id: number, id: number) =>
    api.delete<void>(`/animals/${animal_id}/pregnancies/${id}`),

  // TB Tests
  listTbTests: (animal_id: number) =>
    api.get<TbTest[]>(`/animals/${animal_id}/tb-tests`),
  createTbTest: (animal_id: number, p: CreateTbTestPayload) =>
    api.post<TbTest>(`/animals/${animal_id}/tb-tests`, p),
  patchTbTest: (animal_id: number, id: number, p: PatchTbTestPayload) =>
    api.patch<TbTest>(`/animals/${animal_id}/tb-tests/${id}`, p),
  deleteTbTest: (animal_id: number, id: number) =>
    api.delete<void>(`/animals/${animal_id}/tb-tests/${id}`),

  // Weights
  listWeights: (animal_id: number) =>
    api.get<Weight[]>(`/animals/${animal_id}/weights`),
  createWeight: (animal_id: number, p: CreateWeightPayload) =>
    api.post<Weight>(`/animals/${animal_id}/weights`, p),
  patchWeight: (animal_id: number, id: number, p: PatchWeightPayload) =>
    api.patch<Weight>(`/animals/${animal_id}/weights/${id}`, p),
  deleteWeight: (animal_id: number, id: number) =>
    api.delete<void>(`/animals/${animal_id}/weights/${id}`),

  // Removal
  createRemoval: (animal_id: number, p: CreateRemovalPayload) =>
    api.post<Removal>(`/animals/${animal_id}/removal`, p),
  patchRemoval: (animal_id: number, p: PatchRemovalPayload) =>
    api.patch<Removal>(`/animals/${animal_id}/removal`, p),
  deleteRemoval: (animal_id: number) =>
    api.delete<void>(`/animals/${animal_id}/removal`),
}

export const PREGNANCY_RESULTS = ['pregnant', 'not_pregnant', 'unknown']
export const TB_RESULTS = ['negative', 'positive', 'inconclusive']
export const REMOVAL_REASONS = ['sold', 'died', 'slaughtered', 'other']
export const COMMON_VACCINES = [
  'IBR-BVD', 'Foot & Mouth', 'Brucellosis', 'Anthrax',
  'Neonatal Diarrhea', 'Respiratory', 'Antiparasitic', 'Fly Control',
]
