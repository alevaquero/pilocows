import { api } from './client'

export interface Tag {
  id: number
  tag_number: string
  purchased_at: string
  notes: string
  created_at: string
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
