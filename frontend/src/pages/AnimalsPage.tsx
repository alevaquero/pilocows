import { useState, useEffect, useMemo } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { animalsApi, BREEDS, CATEGORIES, breedLabel, type Animal } from '../api/animals'
import { tagsApi, type Tag } from '../api/tags'
import Modal, { Field, inputCls } from '../components/Modal'

// ── Register modal ────────────────────────────────────────────────────────────

function RegisterAnimalModal({ onClose, onCreated }: { onClose: () => void; onCreated: (a: Animal) => void }) {
  const { t } = useTranslation()
  const [tags, setTags] = useState<Tag[]>([])
  const [tagId, setTagId] = useState<string>('')
  const [breed, setBreed] = useState(BREEDS[0])
  const [category, setCategory] = useState(CATEGORIES[0])
  const [sex, setSex] = useState('female')
  const [dob, setDob] = useState('')
  const [fatherEid, setFatherEid] = useState('')
  const [motherEid, setMotherEid] = useState('')
  const [notes, setNotes] = useState('')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)
  const [animalEids, setAnimalEids] = useState<string[]>([])

  useEffect(() => {
    tagsApi.list(true).then(t => {
      setTags(t)
      if (t.length > 0) setTagId(String(t[0].id))
    })
    animalsApi.list().then(all => setAnimalEids(all.map(a => a.tag_number)))
  }, [])

  const submit = async () => {
    if (!tagId) { setError('Select a tag'); return }
    setSaving(true)
    try {
      const created = await animalsApi.create({
        tag_id: Number(tagId),
        breed, category, sex,
        dob: dob || undefined,
        father_eid: fatherEid || undefined,
        mother_eid: motherEid || undefined,
        notes,
      })
      onCreated(created)
      onClose()
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setSaving(false)
    }
  }

  return (
    <Modal
      title={t('animals.register')}
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
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('animals.breed')}>
          <select className={inputCls} value={breed} onChange={e => setBreed(e.target.value)}>
            {BREEDS.map(b => <option key={b} value={b}>{breedLabel(t, b)}</option>)}
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
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('animals.father_eid')}>
          <input list="animal-eids" className={inputCls} value={fatherEid} onChange={e => setFatherEid(e.target.value)} placeholder={t('animals.parent_placeholder')} />
        </Field>
        <Field label={t('animals.mother_eid')}>
          <input list="animal-eids" className={inputCls} value={motherEid} onChange={e => setMotherEid(e.target.value)} placeholder={t('animals.parent_placeholder')} />
        </Field>
        <datalist id="animal-eids">{animalEids.map(eid => <option key={eid} value={eid} />)}</datalist>
      </div>
      <Field label={t('animals.notes')}>
        <textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} />
      </Field>
    </Modal>
  )
}

// ── Sorting / filtering types ─────────────────────────────────────────────────

type ColKey = 'tag_number' | 'breed' | 'category' | 'sex' | 'is_active' | 'notes'
type SortDir = 'asc' | 'desc'

interface SortState { col: ColKey; dir: SortDir }

interface Filters {
  eid: string
  breed: string
  category: string
  sex: string
  status: string   // '' | 'active' | 'removed'
}

const EMPTY_FILTERS: Filters = { eid: '', breed: '', category: '', sex: '', status: 'active' }

function hasActiveFilters(f: Filters): boolean {
  return f.eid !== '' || f.breed !== '' ||
    f.category !== '' || f.sex !== '' || f.status !== 'active'
}

// ── Sortable column header ─────────────────────────────────────────────────────

function SortTh({
  col, sort, onSort, children,
}: {
  col: ColKey; sort: SortState | null; onSort: (col: ColKey) => void; children: React.ReactNode
}) {
  const active = sort?.col === col
  const icon = !active ? '↕' : sort.dir === 'asc' ? '↑' : '↓'
  return (
    <th
      onClick={() => onSort(col)}
      className="px-4 py-3 text-left text-xs font-semibold text-slate-500 uppercase tracking-wide cursor-pointer select-none hover:text-slate-800 whitespace-nowrap"
    >
      <span className="flex items-center gap-1">
        {children}
        <span className={`text-[10px] ${active ? 'text-slate-700' : 'text-slate-300'}`}>{icon}</span>
      </span>
    </th>
  )
}

// ── Filter cell ────────────────────────────────────────────────────────────────

const filterCls = 'w-full text-xs border border-slate-200 rounded px-2 py-1 bg-white focus:outline-none focus:ring-1 focus:ring-slate-400 text-slate-700'

// ── Main page ─────────────────────────────────────────────────────────────────

export default function AnimalsPage() {
  const { t } = useTranslation()
  const navigate = useNavigate()

  const [allAnimals, setAllAnimals] = useState<Animal[]>([])
  const [loading, setLoading] = useState(true)
  const [showRegister, setShowRegister] = useState(false)
  const [filters, setFilters] = useState<Filters>(EMPTY_FILTERS)
  const [sort, setSort] = useState<SortState | null>(null)

  useEffect(() => {
    setLoading(true)
    animalsApi.list().then(setAllAnimals).finally(() => setLoading(false))
  }, [])

  const set = (key: keyof Filters) => (e: React.ChangeEvent<HTMLInputElement | HTMLSelectElement>) =>
    setFilters(f => ({ ...f, [key]: e.target.value }))

  const toggleSort = (col: ColKey) => {
    setSort(prev => {
      if (prev?.col !== col) return { col, dir: 'asc' }
      if (prev.dir === 'asc')  return { col, dir: 'desc' }
      return null   // third click clears sort
    })
  }

  const visible = useMemo(() => {
    const eid = filters.eid.toLowerCase()

    let rows = allAnimals.filter(a => {
      if (eid  && !a.tag_number.toLowerCase().includes(eid))             return false
      if (filters.breed    && a.breed    !== filters.breed)              return false
      if (filters.category && a.category !== filters.category)           return false
      if (filters.sex      && a.sex      !== filters.sex)                return false
      if (filters.status === 'active'  && !a.is_active)                  return false
      if (filters.status === 'removed' &&  a.is_active)                  return false
      return true
    })

    if (sort) {
      rows = [...rows].sort((a, b) => {
        let av: string | number, bv: string | number
        switch (sort.col) {
          case 'tag_number': av = a.tag_number; bv = b.tag_number; break
          case 'breed':      av = a.breed;      bv = b.breed;      break
          case 'category':   av = a.category;   bv = b.category;   break
          case 'sex':        av = a.sex;        bv = b.sex;        break
          case 'is_active':  av = a.is_active ? 1 : 0; bv = b.is_active ? 1 : 0; break
          case 'notes':      av = a.notes;      bv = b.notes;      break
        }
        const cmp = av < bv ? -1 : av > bv ? 1 : 0
        return sort.dir === 'asc' ? cmp : -cmp
      })
    }

    return rows
  }, [allAnimals, filters, sort])

  return (
    <div className="p-8">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-4">
          <h2 className="text-xl font-semibold text-slate-800">{t('animals.title')}</h2>
          {!loading && (
            <span className="text-xs text-slate-400">
              {t('animals.showing', { visible: visible.length, total: allAnimals.length })}
            </span>
          )}
        </div>
        <div className="flex items-center gap-3">
          {hasActiveFilters(filters) && (
            <button
              onClick={() => setFilters(EMPTY_FILTERS)}
              className="text-xs text-slate-500 hover:text-slate-800 underline"
            >
              {t('animals.clear_filters')}
            </button>
          )}
          <button
            onClick={() => setShowRegister(true)}
            className="px-4 py-2 bg-slate-800 text-white text-sm rounded-lg hover:bg-slate-700"
          >
            {t('animals.register')}
          </button>
        </div>
      </div>

      {/* Table */}
      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              {/* Sortable column headers */}
              <tr className="border-b border-slate-100 bg-slate-50">
                <SortTh col="tag_number" sort={sort} onSort={toggleSort}>EID</SortTh>
                <SortTh col="breed"      sort={sort} onSort={toggleSort}>{t('animals.breed')}</SortTh>
                <SortTh col="category"   sort={sort} onSort={toggleSort}>{t('animals.category')}</SortTh>
                <SortTh col="sex"        sort={sort} onSort={toggleSort}>{t('animals.sex')}</SortTh>
                <SortTh col="is_active"  sort={sort} onSort={toggleSort}>{t('animals.status')}</SortTh>
                <SortTh col="notes"      sort={sort} onSort={toggleSort}>{t('animals.notes')}</SortTh>
              </tr>
              {/* Per-column filter inputs */}
              <tr className="border-b border-slate-200 bg-slate-50">
                <th className="px-3 py-2">
                  <input className={filterCls} value={filters.eid}
                    onChange={set('eid')} placeholder="EID…" />
                </th>
                <th className="px-3 py-2">
                  <select className={filterCls} value={filters.breed} onChange={set('breed')}>
                    <option value="">—</option>
                    {BREEDS.map(b => <option key={b} value={b}>{breedLabel(t, b)}</option>)}
                  </select>
                </th>
                <th className="px-3 py-2">
                  <select className={filterCls} value={filters.category} onChange={set('category')}>
                    <option value="">—</option>
                    {CATEGORIES.map(c => <option key={c} value={c}>{t(`animals.${c}`)}</option>)}
                  </select>
                </th>
                <th className="px-3 py-2">
                  <select className={filterCls} value={filters.sex} onChange={set('sex')}>
                    <option value="">—</option>
                    <option value="male">{t('animals.male')}</option>
                    <option value="female">{t('animals.female')}</option>
                  </select>
                </th>
                <th className="px-3 py-2">
                  <select className={filterCls} value={filters.status} onChange={set('status')}>
                    <option value="">—</option>
                    <option value="active">{t('animals.active')}</option>
                    <option value="removed">{t('animals.removed')}</option>
                  </select>
                </th>
                <th />
              </tr>
            </thead>
            <tbody>
              {visible.length === 0 ? (
                <tr>
                  <td colSpan={6} className="px-4 py-6 text-sm text-slate-400 text-center">
                    {t('animals.no_animals')}
                  </td>
                </tr>
              ) : visible.map(a => (
                <tr
                  key={a.id}
                  className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                  onClick={() => navigate(`/animals/${a.id}`)}
                >
                  <td className="px-4 py-3 font-mono text-slate-800">{a.tag_number}</td>
                  <td className="px-4 py-3 text-slate-600">{breedLabel(t, a.breed)}</td>
                  <td className="px-4 py-3 text-slate-600">{t(`animals.${a.category}`)}</td>
                  <td className="px-4 py-3 text-slate-600">{t(`animals.${a.sex}`)}</td>
                  <td className="px-4 py-3">
                    <span className={`inline-block text-xs font-medium px-2 py-0.5 rounded-full ${
                      a.is_active ? 'bg-green-100 text-green-700' : 'bg-slate-100 text-slate-500'
                    }`}>
                      {a.is_active ? t('animals.active') : t('animals.removed')}
                    </span>
                  </td>
                  <td className="px-4 py-3 text-xs text-slate-400 max-w-[200px] truncate" title={a.notes}>
                    {a.notes || ''}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {showRegister && (
        <RegisterAnimalModal
          onClose={() => setShowRegister(false)}
          onCreated={a => { setAllAnimals(prev => [...prev, a]); setShowRegister(false) }}
        />
      )}
    </div>
  )
}
