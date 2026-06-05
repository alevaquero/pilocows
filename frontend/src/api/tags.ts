import { api } from './client'

export type TagStatus = 'available' | 'active' | 'sold' | 'retired'

export interface Tag {
  id: number
  tag_number: string
  purchased_at: string
  notes: string
  created_at: string
  animal_status?: TagStatus
  animal_id?: number | null
}

export interface CreateTagPayload {
  tag_number: string
  purchased_at: string
  notes?: string
}

export const tagsApi = {
  list: (unassigned?: boolean) =>
    api.get<Tag[]>(`/tags${unassigned ? '?unassigned=true' : ''}`),
  getByNumber: (tag_number: string) =>
    api.get<Tag>(`/tags/${tag_number}`),
  create: (payload: CreateTagPayload) =>
    api.post<Tag>('/tags', payload),
}
