import { useState, useEffect, useCallback, useMemo } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import {
  sessionsApi,
  type Session,
  type SessionDetail,
  type DbSessionRecord,
  type PatchSession,
  sessionTypeKey,
  formatEventData,
} from '../api/sessions'
import { animalsApi, BREEDS, CATEGORIES } from '../api/animals'
import { tagsApi, type Tag } from '../api/tags'
import Modal, { Field, inputCls } from '../components/Modal'

// ── Register Animal modal (used when clicking an unregistered EID) ────────────

function RegisterAnimalModal({
  eid,
  onClose,
  onCreated,
}: {
  eid: string
  onClose: () => void
  onCreated: () => void
}) {
  const { t } = useTranslation()
  const [tags, setTags] = useState<Tag[]>([])
  const [tagId, setTagId] = useState<string>('')
  const [name, setName] = useState('')
  const [breed, setBreed] = useState(BREEDS[0])
  const [category, setCategory] = useState(CATEGORIES[0])
  const [sex, setSex] = useState('female')
  const [dob, setDob] = useState('')
  const [notes, setNotes] = useState('')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    tagsApi.list(true).then(ts => {
      setTags(ts)
      // Pre-select the tag matching this EID if it exists
      const match = ts.find(t => t.tag_number === eid)
      if (match) setTagId(String(match.id))
      else if (ts.length > 0) setTagId(String(ts[0].id))
    })
  }, [eid])

  const submit = async () => {
    if (!tagId) { setError(t('animals.select_tag')); return }
    setSaving(true)
    try {
      await animalsApi.create({
        tag_id: Number(tagId),
        name, breed, category, sex,
        dob: dob || undefined,
        notes,
      })
      onCreated()
      onClose()
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setSaving(false)
    }
  }

  return (
    <Modal
      title={`${t('animals.register')} — ${eid}`}
      onClose={onClose}
      footer={
        <div className="flex gap-2 justify-end w-full">
          <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">
            {t('common.cancel')}
          </button>
          <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">
            {saving ? t('common.loading') : t('common.save')}
          </button>
        </div>
      }
    >
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('animals.tag')}>
        <select className={inputCls} value={tagId} onChange={e => setTagId(e.target.value)}>
          {tags.length === 0
            ? <option value="">{t('animals.select_tag')}</option>
            : tags.map(tag => <option key={tag.id} value={tag.id}>{tag.tag_number}</option>)
          }
        </select>
      </Field>
      <Field label={t('animals.name')}>
        <input className={inputCls} value={name} onChange={e => setName(e.target.value)} />
      </Field>
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('animals.breed')}>
          <select className={inputCls} value={breed} onChange={e => setBreed(e.target.value)}>
            {BREEDS.map(b => <option key={b} value={b}>{b}</option>)}
          </select>
        </Field>
        <Field label={t('animals.category')}>
          <select className={inputCls} value={category} onChange={e => setCategory(e.target.value)}>
            {CATEGORIES.map(c => <option key={c} value={c}>{t(`animals.${c}`)}</option>)}
          </select>
        </Field>
      </div>
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('animals.sex')}>
          <select className={inputCls} value={sex} onChange={e => setSex(e.target.value)}>
            <option value="female">{t('animals.female')}</option>
            <option value="male">{t('animals.male')}</option>
          </select>
        </Field>
        <Field label={t('animals.dob')}>
          <input type="date" className={inputCls} value={dob} onChange={e => setDob(e.target.value)} />
        </Field>
      </div>
      <Field label={t('animals.notes')}>
        <textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} />
      </Field>
    </Modal>
  )
}

// ── Edit session modal ────────────────────────────────────────────────────────

function EditSessionModal({
  session,
  onClose,
  onSaved,
}: {
  session: SessionDetail
  onClose: () => void
  onSaved: (updated: Session) => void
}) {
  const { t } = useTranslation()
  const [farm, setFarm] = useState(session.farm)
  const [operator, setOperator] = useState(session.operator)
  const [comments, setComments] = useState(session.comments)
  const [handheldNote, setHandheldNote] = useState(session.handheld_note)
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try {
      const updated = await sessionsApi.patch(session.id, {
        farm, operator, comments, handheld_note: handheldNote,
      } as PatchSession)
      onSaved(updated)
      onClose()
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setSaving(false)
    }
  }

  return (
    <Modal
      title={t('sessions.edit_session')}
      onClose={onClose}
      footer={
        <div className="flex gap-2 justify-end w-full">
          <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">
            {t('common.cancel')}
          </button>
          <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">
            {saving ? t('common.loading') : t('common.save')}
          </button>
        </div>
      }
    >
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('sessions.farm')}>
        <input className={inputCls} value={farm} onChange={e => setFarm(e.target.value)} />
      </Field>
      <Field label={t('sessions.operator')}>
        <input className={inputCls} value={operator} onChange={e => setOperator(e.target.value)} />
      </Field>
      <Field label={t('sessions.comments')}>
        <textarea className={inputCls} rows={3} value={comments} onChange={e => setComments(e.target.value)} />
      </Field>
      <Field label={t('sessions.handheld_note')}>
        <textarea className={inputCls} rows={2} value={handheldNote} onChange={e => setHandheldNote(e.target.value)} />
      </Field>
    </Modal>
  )
}

// ── Delete confirmation modal ─────────────────────────────────────────────────

function DeleteConfirmModal({ onClose, onConfirm }: { onClose: () => void; onConfirm: () => void }) {
  const { t } = useTranslation()
  return (
    <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div className="bg-white rounded-2xl p-6 w-80 shadow-xl">
        <p className="text-sm text-slate-700 mb-6">{t('sessions.delete_confirm')}</p>
        <div className="flex gap-3">
          <button
            onClick={onClose}
            className="flex-1 py-2 text-sm rounded-xl border border-slate-200 text-slate-600 hover:bg-slate-50"
          >
            {t('common.cancel')}
          </button>
          <button
            onClick={onConfirm}
            className="flex-1 py-2 text-sm rounded-xl bg-red-600 text-white hover:bg-red-700"
          >
            {t('sessions.delete_session')}
          </button>
        </div>
      </div>
    </div>
  )
}

// ── Sort / filter types ───────────────────────────────────────────────────────

type ColKey = 'eid' | 'animal_name' | 'scanned_at' | 'event_summary' | 'note'
type SortDir = 'asc' | 'desc'
interface SortState { col: ColKey; dir: SortDir }
interface Filters { eid: string; animal: string }
const EMPTY_FILTERS: Filters = { eid: '', animal: '' }

// ── Sortable column header ────────────────────────────────────────────────────

function SortTh({
  col, sort, onSort, children, className = '',
}: {
  col: ColKey; sort: SortState | null; onSort: (c: ColKey) => void
  children: React.ReactNode; className?: string
}) {
  const active = sort?.col === col
  const icon = !active ? '↕' : sort.dir === 'asc' ? '↑' : '↓'
  return (
    <th
      onClick={() => onSort(col)}
      className={`px-4 py-2.5 text-left text-xs font-semibold text-slate-500 uppercase tracking-wide cursor-pointer select-none hover:text-slate-800 whitespace-nowrap ${className}`}
    >
      <span className="flex items-center gap-1">
        {children}
        <span className={`text-[10px] ${active ? 'text-slate-700' : 'text-slate-300'}`}>{icon}</span>
      </span>
    </th>
  )
}

const filterCls = 'w-full text-xs border border-slate-200 rounded px-2 py-1 bg-white focus:outline-none focus:ring-1 focus:ring-slate-400 text-slate-700'

// ── Record row ────────────────────────────────────────────────────────────────

function RecordRow({
  record,
  eventSummary,
  onClick,
}: {
  record: DbSessionRecord
  eventSummary: string
  onClick: () => void
}) {
  const time = new Date(record.scanned_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })

  return (
    <tr
      className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
      onClick={onClick}
    >
      <td className="px-4 py-2.5 font-mono text-xs text-slate-800">{record.eid}</td>
      <td className="px-4 py-2.5 text-sm text-slate-700">
        <span className={record.animal_name ? 'font-medium' : 'text-slate-400'}>
          {record.animal_name || '—'}
        </span>
      </td>
      <td className="px-4 py-2.5 text-xs text-slate-500 whitespace-nowrap">{time}</td>
      <td className="px-4 py-2.5 text-sm text-slate-700">{eventSummary || '—'}</td>
      <td className="px-4 py-2.5 text-xs text-slate-400 max-w-[160px] truncate" title={record.note}>
        {record.note || ''}
      </td>
    </tr>
  )
}

// ── Page ──────────────────────────────────────────────────────────────────────

export default function SessionDetailPage() {
  const { id } = useParams<{ id: string }>()
  const { t } = useTranslation()
  const navigate = useNavigate()

  const [session, setSession] = useState<SessionDetail | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')

  const [showEdit, setShowEdit] = useState(false)
  const [showDelete, setShowDelete] = useState(false)
  const [registerEid, setRegisterEid] = useState<string | null>(null)

  const [filters, setFilters] = useState<Filters>(EMPTY_FILTERS)
  const [sort, setSort] = useState<SortState | null>(null)

  useEffect(() => {
    const numId = Number(id)
    if (!numId) { setError('Invalid session id'); setLoading(false); return }
    sessionsApi.get(numId)
      .then(setSession)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [id])

  const handleDelete = async () => {
    if (!session) return
    try {
      await sessionsApi.delete(session.id)
      navigate('/sessions', { replace: true })
    } catch (e) {
      setError(String(e))
      setShowDelete(false)
    }
  }

  const handleRowClick = useCallback((record: DbSessionRecord) => {
    if (record.animal_id != null) {
      navigate(`/animals/${record.animal_id}`)
    } else {
      setRegisterEid(record.eid)
    }
  }, [navigate])

  const toggleSort = (col: ColKey) => {
    setSort(prev => {
      if (prev?.col !== col) return { col, dir: 'asc' }
      if (prev.dir === 'asc')  return { col, dir: 'desc' }
      return null
    })
  }

  const visible = useMemo(() => {
    if (!session) return []
    const eid    = filters.eid.toLowerCase()
    const animal = filters.animal.toLowerCase()

    let rows = session.records.filter(r => {
      if (eid    && !r.eid.toLowerCase().includes(eid))                          return false
      if (animal && !(r.animal_name ?? '').toLowerCase().includes(animal))       return false
      return true
    })

    if (sort) {
      rows = [...rows].sort((a, b) => {
        let av: string, bv: string
        switch (sort.col) {
          case 'eid':          av = a.eid;          bv = b.eid;          break
          case 'animal_name':  av = a.animal_name ?? ''; bv = b.animal_name ?? ''; break
          case 'scanned_at':   av = a.scanned_at;   bv = b.scanned_at;   break
          case 'event_summary':
            av = formatEventData(a.event_data, session.session_type)
            bv = formatEventData(b.event_data, session.session_type)
            break
          case 'note': av = a.note; bv = b.note; break
        }
        const cmp = av < bv ? -1 : av > bv ? 1 : 0
        return sort.dir === 'asc' ? cmp : -cmp
      })
    }

    return rows
  }, [session, filters, sort])

  if (loading) {
    return <div className="p-8 text-slate-500 text-sm">{t('common.loading')}</div>
  }

  if (error || !session) {
    return (
      <div className="p-8">
        <button onClick={() => navigate(-1)} className="text-xs text-slate-400 hover:text-slate-700 mb-4 block">
          ← {t('common.back')}
        </button>
        <div className="bg-red-50 border border-red-200 rounded-xl p-4">
          <p className="text-sm text-red-700">{error || 'Session not found'}</p>
        </div>
      </div>
    )
  }

  const date = new Date(session.created_at).toLocaleDateString()
  const typeKey = `sessions.type_${sessionTypeKey(session.session_type)}`

  return (
    <div className="p-8">
      {/* Back link */}
      <button
        onClick={() => navigate('/sessions')}
        className="text-xs text-slate-400 hover:text-slate-700 mb-5 block"
      >
        ← {t('sessions.title')}
      </button>

      {/* Header */}
      <div className="flex items-start justify-between gap-4 mb-6">
        <div>
          <div className="flex items-center gap-2 flex-wrap mb-1">
            <h2 className="text-xl font-semibold text-slate-800">{session.name || '—'}</h2>
            <span className="text-xs font-medium px-2 py-0.5 rounded-full bg-blue-100 text-blue-700">
              {t(typeKey)}
            </span>
            <span className={`text-xs font-medium px-2 py-0.5 rounded-full ${
              session.status === 0
                ? 'bg-green-50 text-green-600'
                : 'bg-slate-100 text-slate-500'
            }`}>
              {session.status === 0 ? t('sessions.status_open') : t('sessions.status_closed')}
            </span>
          </div>
          <p className="text-xs text-slate-400">{date} · {session.device_id}</p>
        </div>

        <div className="flex items-center gap-2 shrink-0">
          <button
            onClick={() => setShowEdit(true)}
            className="px-3 py-1.5 text-xs rounded-lg border border-slate-200 text-slate-600 hover:bg-slate-50"
          >
            {t('sessions.edit_session')}
          </button>
          <button
            onClick={() => setShowDelete(true)}
            className="px-3 py-1.5 text-xs rounded-lg border border-red-200 text-red-600 hover:bg-red-50"
          >
            {t('sessions.delete_session')}
          </button>
        </div>
      </div>

      {/* Metadata cards */}
      <div className="grid grid-cols-2 gap-3 mb-6 md:grid-cols-4">
        <MetaCard label={t('sessions.farm')} value={session.farm} />
        <MetaCard label={t('sessions.operator')} value={session.operator} />
        <MetaCard label={t('sessions.comments')} value={session.comments} wide />
        {session.handheld_note && (
          <MetaCard label={t('sessions.handheld_note')} value={session.handheld_note} wide />
        )}
      </div>

      {/* Records table */}
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-sm font-semibold text-slate-600 uppercase tracking-wide">
          {t('sessions.records_title')}
          {session.records.length > 0 && (
            <span className="ml-2 font-normal text-slate-400 normal-case tracking-normal">
              {visible.length === session.records.length
                ? `(${session.records.length})`
                : `(${visible.length} / ${session.records.length})`}
            </span>
          )}
        </h3>
        {(filters.eid || filters.animal) && (
          <button
            onClick={() => setFilters(EMPTY_FILTERS)}
            className="text-xs text-slate-500 hover:text-slate-800 underline"
          >
            {t('animals.clear_filters')}
          </button>
        )}
      </div>

      {session.records.length === 0 ? (
        <p className="text-sm text-slate-400">{t('detail.no_records')}</p>
      ) : (
        <div className="bg-white rounded-xl border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <SortTh col="eid"          sort={sort} onSort={toggleSort}>{t('sessions.col_eid')}</SortTh>
                <SortTh col="animal_name"  sort={sort} onSort={toggleSort}>{t('sessions.col_animal')}</SortTh>
                <SortTh col="scanned_at"   sort={sort} onSort={toggleSort}>{t('sessions.col_time')}</SortTh>
                <SortTh col="event_summary" sort={sort} onSort={toggleSort}>{t('sessions.col_data')}</SortTh>
                <SortTh col="note"         sort={sort} onSort={toggleSort}>{t('sessions.col_note')}</SortTh>
              </tr>
              <tr className="border-b border-slate-200 bg-slate-50">
                <th className="px-3 py-2">
                  <input className={filterCls} value={filters.eid}
                    onChange={e => setFilters(f => ({ ...f, eid: e.target.value }))}
                    placeholder="EID…" />
                </th>
                <th className="px-3 py-2">
                  <input className={filterCls} value={filters.animal}
                    onChange={e => setFilters(f => ({ ...f, animal: e.target.value }))}
                    placeholder={`${t('sessions.col_animal')}…`} />
                </th>
                <th /><th /><th />
              </tr>
            </thead>
            <tbody>
              {visible.length === 0 ? (
                <tr>
                  <td colSpan={5} className="px-4 py-6 text-sm text-slate-400 text-center">
                    {t('animals.no_animals')}
                  </td>
                </tr>
              ) : visible.map(r => (
                <RecordRow
                  key={r.id}
                  record={r}
                  eventSummary={formatEventData(r.event_data, session.session_type)}
                  onClick={() => handleRowClick(r)}
                />
              ))}
            </tbody>
          </table>
        </div>
      )}

      {/* Modals */}
      {showEdit && (
        <EditSessionModal
          session={session}
          onClose={() => setShowEdit(false)}
          onSaved={(updated: Session) =>
            setSession(prev => prev ? { ...updated, records: prev.records } : prev)
          }
        />
      )}
      {showDelete && (
        <DeleteConfirmModal
          onClose={() => setShowDelete(false)}
          onConfirm={handleDelete}
        />
      )}
      {registerEid && (
        <RegisterAnimalModal
          eid={registerEid}
          onClose={() => setRegisterEid(null)}
          onCreated={() => {
            // Reload session to reflect the newly registered animal
            sessionsApi.get(session.id).then(setSession).catch(() => {})
          }}
        />
      )}
    </div>
  )
}

// ── MetaCard ──────────────────────────────────────────────────────────────────

function MetaCard({ label, value, wide }: { label: string; value: string; wide?: boolean }) {
  if (!value) return null
  return (
    <div className={`bg-white rounded-xl border border-slate-200 px-4 py-3 ${wide ? 'col-span-2' : ''}`}>
      <p className="text-xs text-slate-400 mb-0.5">{label}</p>
      <p className="text-sm text-slate-800">{value}</p>
    </div>
  )
}
