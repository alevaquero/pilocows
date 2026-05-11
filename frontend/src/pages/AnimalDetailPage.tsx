import { useState, useEffect, useCallback } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { animalsApi, type AnimalProfile, BREEDS, CATEGORIES } from '../api/animals'
import {
  healthApi,
  PREGNANCY_RESULTS, TB_RESULTS, REMOVAL_REASONS, COMMON_VACCINES,
  type Vaccination, type Pregnancy, type TbTest, type Weight, type Removal,
} from '../api/health'
import Modal, { Field, inputCls } from '../components/Modal'

// ── Types ─────────────────────────────────────────────────────────────────────

type Tab = 'vaccinations' | 'pregnancies' | 'tb_tests' | 'weights'

type ModalState =
  | null
  | { kind: 'edit_animal' }
  | { kind: 'vaccination'; initial?: Vaccination }
  | { kind: 'pregnancy'; initial?: Pregnancy }
  | { kind: 'tb_test'; initial?: TbTest }
  | { kind: 'weight'; initial?: Weight }
  | { kind: 'remove' }
  | { kind: 'edit_removal' }
  | { kind: 'restore' }
  | { kind: 'delete'; recordType: Tab; id: number }

const TAB_TO_KIND: Record<Tab, 'vaccination' | 'pregnancy' | 'tb_test' | 'weight'> = {
  vaccinations: 'vaccination',
  pregnancies: 'pregnancy',
  tb_tests: 'tb_test',
  weights: 'weight',
}

// ── Small helpers ─────────────────────────────────────────────────────────────

function TabButton({ label, active, onClick }: { label: string; active: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      className={`px-4 py-2 text-sm font-medium border-b-2 transition-colors ${
        active ? 'border-slate-800 text-slate-800' : 'border-transparent text-slate-500 hover:text-slate-700'
      }`}
    >
      {label}
    </button>
  )
}

function Th({ children }: { children: React.ReactNode }) {
  return <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{children}</th>
}
function Td({ children }: { children: React.ReactNode }) {
  return <td className="px-4 py-3 text-slate-600 select-text cursor-text">{children}</td>
}
function Empty({ t }: { t: (k: string) => string }) {
  return <p className="px-4 py-6 text-sm text-slate-400">{t('detail.no_records')}</p>
}
function RowActions({ onEdit, onDelete }: { onEdit: () => void; onDelete: () => void }) {
  const { t } = useTranslation()
  return (
    <td className="px-4 py-3 text-right whitespace-nowrap">
      <button onClick={onEdit} className="text-xs text-slate-500 hover:text-slate-800 mr-3">{t('detail.edit')}</button>
      <button onClick={onDelete} className="text-xs text-red-500 hover:text-red-700">{t('detail.delete')}</button>
    </td>
  )
}
function today() { return new Date().toISOString().slice(0, 10) }

// ── Edit Animal modal ─────────────────────────────────────────────────────────

function EditAnimalModal({ profile, onClose, onSaved }: {
  profile: AnimalProfile; onClose: () => void; onSaved: (a: AnimalProfile) => void
}) {
  const { t } = useTranslation()
  const [breed, setBreed] = useState(profile.breed)
  const [category, setCategory] = useState(profile.category)
  const [sex, setSex] = useState(profile.sex)
  const [dob, setDob] = useState(profile.dob ?? '')
  const [notes, setNotes] = useState(profile.notes)
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try {
      const updated = await animalsApi.patch(profile.id, {
        breed, category, sex, dob: dob || undefined, notes,
      })
      onSaved({ ...profile, ...updated })
      onClose()
    } catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('detail.edit_animal')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">{saving ? t('common.loading') : t('common.save')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('animals.breed')}>
          <input list="breeds" className={inputCls} value={breed} onChange={e => setBreed(e.target.value)} />
          <datalist id="breeds">{BREEDS.map(b => <option key={b} value={b} />)}</datalist>
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
            <option value="male">{t('animals.male')}</option>
            <option value="female">{t('animals.female')}</option>
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

// ── Vaccination modal (add + edit) ────────────────────────────────────────────

function VaccinationModal({ animalId, initial, onClose, onSaved }: {
  animalId: number; initial?: Vaccination; onClose: () => void
  onSaved: (v: Vaccination, isEdit: boolean) => void
}) {
  const { t } = useTranslation()
  const [vaccine, setVaccine] = useState(initial?.vaccine ?? COMMON_VACCINES[0])
  const [dose, setDose] = useState(initial?.dose ?? '')
  const [administeredAt, setAdministeredAt] = useState(initial?.administered_at ?? today())
  const [nextDueAt, setNextDueAt] = useState(initial?.next_due_at ?? '')
  const [notes, setNotes] = useState(initial?.notes ?? '')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try {
      const payload = { vaccine, dose, administered_at: administeredAt, next_due_at: nextDueAt || undefined, notes }
      const result = initial
        ? await healthApi.patchVaccination(animalId, initial.id, payload)
        : await healthApi.createVaccination(animalId, payload)
      onSaved(result, !!initial)
      onClose()
    } catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('detail.add')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">{saving ? t('common.loading') : t('common.save')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('vaccination.vaccine')}>
        <input list="vaccines" className={inputCls} value={vaccine} onChange={e => setVaccine(e.target.value)} />
        <datalist id="vaccines">{COMMON_VACCINES.map(v => <option key={v} value={v} />)}</datalist>
      </Field>
      <Field label={t('vaccination.dose')}>
        <input className={inputCls} value={dose} onChange={e => setDose(e.target.value)} placeholder="e.g. 5ml" />
      </Field>
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('vaccination.administered_at')}><input type="date" className={inputCls} value={administeredAt} onChange={e => setAdministeredAt(e.target.value)} /></Field>
        <Field label={t('vaccination.next_due_at')}><input type="date" className={inputCls} value={nextDueAt} onChange={e => setNextDueAt(e.target.value)} /></Field>
      </div>
      <Field label={t('vaccination.notes')}><textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} /></Field>
    </Modal>
  )
}

// ── Pregnancy modal (add + edit) ──────────────────────────────────────────────

function PregnancyModal({ animalId, initial, onClose, onSaved }: {
  animalId: number; initial?: Pregnancy; onClose: () => void
  onSaved: (p: Pregnancy, isEdit: boolean) => void
}) {
  const { t } = useTranslation()
  const [result, setResult] = useState(initial?.result ?? PREGNANCY_RESULTS[0])
  const [checkedAt, setCheckedAt] = useState(initial?.checked_at ?? today())
  const [dueDate, setDueDate] = useState(initial?.due_date ?? '')
  const [notes, setNotes] = useState(initial?.notes ?? '')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try {
      const payload = { result, checked_at: checkedAt, due_date: dueDate || undefined, notes }
      const res = initial
        ? await healthApi.patchPregnancy(animalId, initial.id, payload)
        : await healthApi.createPregnancy(animalId, payload)
      onSaved(res, !!initial)
      onClose()
    } catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('detail.add')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">{saving ? t('common.loading') : t('common.save')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('pregnancy.result')}>
        <select className={inputCls} value={result} onChange={e => setResult(e.target.value)}>
          {PREGNANCY_RESULTS.map(r => <option key={r} value={r}>{t(`pregnancy.${r}`)}</option>)}
        </select>
      </Field>
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('pregnancy.checked_at')}><input type="date" className={inputCls} value={checkedAt} onChange={e => setCheckedAt(e.target.value)} /></Field>
        <Field label={t('pregnancy.due_date')}><input type="date" className={inputCls} value={dueDate} onChange={e => setDueDate(e.target.value)} /></Field>
      </div>
      <Field label={t('pregnancy.notes')}><textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} /></Field>
    </Modal>
  )
}

// ── TB Test modal (add + edit) ────────────────────────────────────────────────

function TbTestModal({ animalId, initial, onClose, onSaved }: {
  animalId: number; initial?: TbTest; onClose: () => void
  onSaved: (tb: TbTest, isEdit: boolean) => void
}) {
  const { t } = useTranslation()
  const [result, setResult] = useState(initial?.result ?? TB_RESULTS[0])
  const [testedAt, setTestedAt] = useState(initial?.tested_at ?? today())
  const [notes, setNotes] = useState(initial?.notes ?? '')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try {
      const payload = { result, tested_at: testedAt, notes }
      const res = initial
        ? await healthApi.patchTbTest(animalId, initial.id, payload)
        : await healthApi.createTbTest(animalId, payload)
      onSaved(res, !!initial)
      onClose()
    } catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('detail.add')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">{saving ? t('common.loading') : t('common.save')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('tb_test.result')}>
        <select className={inputCls} value={result} onChange={e => setResult(e.target.value)}>
          {TB_RESULTS.map(r => <option key={r} value={r}>{t(`tb_test.${r}`)}</option>)}
        </select>
      </Field>
      <Field label={t('tb_test.tested_at')}><input type="date" className={inputCls} value={testedAt} onChange={e => setTestedAt(e.target.value)} /></Field>
      <Field label={t('tb_test.notes')}><textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} /></Field>
    </Modal>
  )
}

// ── Weight modal (add + edit) ─────────────────────────────────────────────────

function WeightModal({ animalId, initial, onClose, onSaved }: {
  animalId: number; initial?: Weight; onClose: () => void
  onSaved: (w: Weight, isEdit: boolean) => void
}) {
  const { t } = useTranslation()
  const [weightKg, setWeightKg] = useState(initial ? String(initial.weight_kg) : '')
  const [weighedAt, setWeighedAt] = useState(initial?.weighed_at ?? today())
  const [notes, setNotes] = useState(initial?.notes ?? '')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    if (!weightKg || isNaN(Number(weightKg))) { setError('Invalid weight'); return }
    setSaving(true)
    try {
      const payload = { weight_kg: Number(weightKg), weighed_at: weighedAt, notes }
      const res = initial
        ? await healthApi.patchWeight(animalId, initial.id, payload)
        : await healthApi.createWeight(animalId, payload)
      onSaved(res, !!initial)
      onClose()
    } catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('detail.add')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">{saving ? t('common.loading') : t('common.save')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <div className="grid grid-cols-2 gap-3">
        <Field label={t('weight.weight_kg')}><input type="number" step="0.1" className={inputCls} value={weightKg} onChange={e => setWeightKg(e.target.value)} /></Field>
        <Field label={t('weight.weighed_at')}><input type="date" className={inputCls} value={weighedAt} onChange={e => setWeighedAt(e.target.value)} /></Field>
      </div>
      <Field label={t('weight.notes')}><textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} /></Field>
    </Modal>
  )
}

// ── Remove / restore modals (unchanged) ───────────────────────────────────────

function RemoveModal({ animalId, onClose, onDone }: {
  animalId: number; onClose: () => void; onDone: () => void
}) {
  const { t } = useTranslation()
  const [reason, setReason] = useState(REMOVAL_REASONS[0])
  const [removedAt, setRemovedAt] = useState(today())
  const [notes, setNotes] = useState('')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try { await healthApi.createRemoval(animalId, { reason, removed_at: removedAt, notes }); onDone() }
    catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('removal.confirm')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-red-600 text-white hover:bg-red-700 disabled:opacity-50">{saving ? t('common.loading') : t('detail.remove')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('removal.reason')}>
        <select className={inputCls} value={reason} onChange={e => setReason(e.target.value)}>
          {REMOVAL_REASONS.map(r => <option key={r} value={r}>{t(`removal.${r}`)}</option>)}
        </select>
      </Field>
      <Field label={t('removal.removed_at')}><input type="date" className={inputCls} value={removedAt} onChange={e => setRemovedAt(e.target.value)} /></Field>
      <Field label={t('removal.notes')}><textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} /></Field>
    </Modal>
  )
}

function EditRemovalModal({ animalId, current, onClose, onDone }: {
  animalId: number; current: Removal; onClose: () => void; onDone: (r: Removal) => void
}) {
  const { t } = useTranslation()
  const [reason, setReason] = useState(current.reason)
  const [removedAt, setRemovedAt] = useState(current.removed_at)
  const [notes, setNotes] = useState(current.notes)
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try { const updated = await healthApi.patchRemoval(animalId, { reason, removed_at: removedAt, notes }); onDone(updated) }
    catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('removal.edit')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50">{saving ? t('common.loading') : t('common.save')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('removal.reason')}>
        <select className={inputCls} value={reason} onChange={e => setReason(e.target.value)}>
          {REMOVAL_REASONS.map(r => <option key={r} value={r}>{t(`removal.${r}`)}</option>)}
        </select>
      </Field>
      <Field label={t('removal.removed_at')}><input type="date" className={inputCls} value={removedAt} onChange={e => setRemovedAt(e.target.value)} /></Field>
      <Field label={t('removal.notes')}><textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} /></Field>
    </Modal>
  )
}

function RestoreModal({ animalId, onClose, onDone }: {
  animalId: number; onClose: () => void; onDone: () => void
}) {
  const { t } = useTranslation()
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    setSaving(true)
    try { await healthApi.deleteRemoval(animalId); onDone() }
    catch (e) { setError(String(e)) }
    finally { setSaving(false) }
  }

  return (
    <Modal title={t('removal.restore')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-green-600 text-white hover:bg-green-700 disabled:opacity-50">{saving ? t('common.loading') : t('removal.restore')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <p className="text-sm text-slate-600">{t('removal.restore_confirm')}</p>
    </Modal>
  )
}

// ── Delete confirm modal ──────────────────────────────────────────────────────

function DeleteConfirmModal({ onConfirm, onClose }: {
  onConfirm: () => Promise<void>; onClose: () => void
}) {
  const { t } = useTranslation()
  const [saving, setSaving] = useState(false)
  const [error, setError] = useState('')

  const submit = async () => {
    setSaving(true)
    try { await onConfirm(); onClose() }
    catch (e) { setError(String(e)); setSaving(false) }
  }

  return (
    <Modal title={t('detail.delete_confirm')} onClose={onClose} footer={
      <div className="flex gap-2 justify-end w-full">
        <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">{t('common.cancel')}</button>
        <button onClick={submit} disabled={saving} className="px-4 py-2 text-sm rounded-lg bg-red-600 text-white hover:bg-red-700 disabled:opacity-50">{saving ? t('common.loading') : t('detail.delete')}</button>
      </div>
    }>
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
    </Modal>
  )
}

// ── Main page ─────────────────────────────────────────────────────────────────

export default function AnimalDetailPage() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()
  const { t } = useTranslation()
  const [profile, setProfile] = useState<AnimalProfile | null>(null)
  const [tab, setTab] = useState<Tab>('vaccinations')
  const [modal, setModal] = useState<ModalState>(null)
  const [loading, setLoading] = useState(true)

  const load = useCallback(() => {
    if (!id) return
    setLoading(true)
    animalsApi.get(Number(id)).then(setProfile).finally(() => setLoading(false))
  }, [id])

  useEffect(() => { load() }, [load])

  if (loading) return <div className="p-8 text-sm text-slate-500">{t('common.loading')}</div>
  if (!profile) return <div className="p-8 text-sm text-slate-500">{t('common.error')}</div>

  const { vaccinations, pregnancies, tb_tests, weights, removal } = profile

  // ── Profile state updaters ─────────────────────────────────────────────────

  const replaceVax = (v: Vaccination) =>
    setProfile(p => p ? { ...p, vaccinations: p.vaccinations.map(x => x.id === v.id ? v : x) } : p)
  const addVax = (v: Vaccination) =>
    setProfile(p => p ? { ...p, vaccinations: [v, ...p.vaccinations] } : p)
  const deleteVax = (id: number) =>
    setProfile(p => p ? { ...p, vaccinations: p.vaccinations.filter(x => x.id !== id) } : p)

  const replacePreg = (pg: Pregnancy) =>
    setProfile(p => p ? { ...p, pregnancies: p.pregnancies.map(x => x.id === pg.id ? pg : x) } : p)
  const addPreg = (pg: Pregnancy) =>
    setProfile(p => p ? { ...p, pregnancies: [pg, ...p.pregnancies] } : p)
  const deletePreg = (id: number) =>
    setProfile(p => p ? { ...p, pregnancies: p.pregnancies.filter(x => x.id !== id) } : p)

  const replaceTb = (tb: TbTest) =>
    setProfile(p => p ? { ...p, tb_tests: p.tb_tests.map(x => x.id === tb.id ? tb : x) } : p)
  const addTb = (tb: TbTest) =>
    setProfile(p => p ? { ...p, tb_tests: [tb, ...p.tb_tests] } : p)
  const deleteTb = (id: number) =>
    setProfile(p => p ? { ...p, tb_tests: p.tb_tests.filter(x => x.id !== id) } : p)

  const replaceWeight = (w: Weight) =>
    setProfile(p => p ? { ...p, weights: p.weights.map(x => x.id === w.id ? w : x) } : p)
  const addWeight = (w: Weight) =>
    setProfile(p => p ? { ...p, weights: [w, ...p.weights] } : p)
  const deleteWeight = (id: number) =>
    setProfile(p => p ? { ...p, weights: p.weights.filter(x => x.id !== id) } : p)

  // ── Delete dispatch ────────────────────────────────────────────────────────

  const deleteRecord = async (recordType: Tab, recordId: number) => {
    switch (recordType) {
      case 'vaccinations': await healthApi.deleteVaccination(profile.id, recordId); deleteVax(recordId); break
      case 'pregnancies':  await healthApi.deletePregnancy(profile.id, recordId);  deletePreg(recordId); break
      case 'tb_tests':     await healthApi.deleteTbTest(profile.id, recordId);     deleteTb(recordId);   break
      case 'weights':      await healthApi.deleteWeight(profile.id, recordId);     deleteWeight(recordId); break
    }
  }

  return (
    <div className="p-8">
      <button onClick={() => navigate('/animals')} className="text-sm text-slate-500 hover:text-slate-800 mb-4">
        ← {t('common.back')}
      </button>

      {/* Profile header */}
      <div className="bg-white rounded-xl shadow-sm border border-slate-200 p-6 mb-6">
        <div className="flex items-start justify-between">
          <div>
            <h2 className="text-2xl font-bold font-mono text-slate-800 select-text cursor-text mb-1">{profile.tag_number}</h2>
            <p className="text-slate-500 select-text cursor-text">
              {profile.breed} · {t(`animals.${profile.category}`)} · {t(`animals.${profile.sex}`)}{profile.dob ? ` · ${profile.dob}` : ''}
            </p>
            {profile.notes && <p className="text-sm text-slate-400 mt-1 select-text cursor-text">{profile.notes}</p>}
          </div>
          <div className="flex items-center gap-2">
            <span className={`text-xs font-medium px-2 py-1 rounded-full ${
              profile.is_active ? 'bg-green-100 text-green-700' : 'bg-slate-100 text-slate-500'
            }`}>
              {profile.is_active ? t('animals.active') : t('animals.removed')}
            </span>
            <button
              onClick={() => setModal({ kind: 'edit_animal' })}
              className="px-3 py-1.5 text-xs rounded-lg border border-slate-200 text-slate-600 hover:bg-slate-50"
            >
              {t('detail.edit_animal')}
            </button>
            {profile.is_active && !removal && (
              <button
                onClick={() => setModal({ kind: 'remove' })}
                className="px-3 py-1.5 text-xs rounded-lg border border-red-200 text-red-600 hover:bg-red-50"
              >
                {t('detail.remove')}
              </button>
            )}
          </div>
        </div>
        {removal && (
          <div className="mt-4 pt-4 border-t border-slate-100 flex items-center justify-between gap-4">
            <p className="text-sm text-slate-500 select-text cursor-text">
              {t(`removal.${removal.reason}`)} · {removal.removed_at}{removal.notes ? ` · ${removal.notes}` : ''}
            </p>
            <div className="flex gap-2 shrink-0">
              <button onClick={() => setModal({ kind: 'edit_removal' })} className="px-3 py-1.5 text-xs rounded-lg border border-slate-200 text-slate-600 hover:bg-slate-50">{t('removal.edit')}</button>
              <button onClick={() => setModal({ kind: 'restore' })} className="px-3 py-1.5 text-xs rounded-lg border border-green-200 text-green-700 hover:bg-green-50">{t('removal.restore')}</button>
            </div>
          </div>
        )}
      </div>

      {/* Tabs */}
      <div className="flex border-b border-slate-200 mb-6 gap-1">
        <TabButton label={`${t('detail.vaccinations')} (${vaccinations.length})`} active={tab === 'vaccinations'} onClick={() => setTab('vaccinations')} />
        <TabButton label={`${t('detail.pregnancies')} (${pregnancies.length})`} active={tab === 'pregnancies'} onClick={() => setTab('pregnancies')} />
        <TabButton label={`${t('detail.tb_tests')} (${tb_tests.length})`} active={tab === 'tb_tests'} onClick={() => setTab('tb_tests')} />
        <TabButton label={`${t('detail.weights')} (${weights.length})`} active={tab === 'weights'} onClick={() => setTab('weights')} />
      </div>

      {/* Tab content */}
      <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
        <div className="px-4 py-3 border-b border-slate-100 flex justify-end">
          <button onClick={() => setModal({ kind: TAB_TO_KIND[tab] })} className="px-3 py-1.5 text-xs rounded-lg bg-slate-800 text-white hover:bg-slate-700">
            {t('detail.add')}
          </button>
        </div>

        {/* Vaccinations */}
        {tab === 'vaccinations' && (
          vaccinations.length === 0 ? <Empty t={t} /> : (
            <table className="w-full text-sm">
              <thead><tr className="border-b border-slate-100 bg-slate-50">
                <Th>{t('vaccination.vaccine')}</Th><Th>{t('vaccination.dose')}</Th>
                <Th>{t('vaccination.administered_at')}</Th><Th>{t('vaccination.next_due_at')}</Th>
                <Th>{t('vaccination.notes')}</Th><th />
              </tr></thead>
              <tbody>{vaccinations.map(v => (
                <tr key={v.id} className="border-b border-slate-50 last:border-0">
                  <td className="px-4 py-3 font-medium text-slate-800 select-text cursor-text">{v.vaccine}</td>
                  <Td>{v.dose || '—'}</Td><Td>{v.administered_at}</Td>
                  <Td>{v.next_due_at ?? '—'}</Td><Td>{v.notes || '—'}</Td>
                  <RowActions
                    onEdit={() => setModal({ kind: 'vaccination', initial: v })}
                    onDelete={() => setModal({ kind: 'delete', recordType: 'vaccinations', id: v.id })}
                  />
                </tr>
              ))}</tbody>
            </table>
          )
        )}

        {/* Pregnancies */}
        {tab === 'pregnancies' && (
          pregnancies.length === 0 ? <Empty t={t} /> : (
            <table className="w-full text-sm">
              <thead><tr className="border-b border-slate-100 bg-slate-50">
                <Th>{t('pregnancy.result')}</Th><Th>{t('pregnancy.checked_at')}</Th>
                <Th>{t('pregnancy.due_date')}</Th><Th>{t('pregnancy.notes')}</Th><th />
              </tr></thead>
              <tbody>{pregnancies.map(p => (
                <tr key={p.id} className="border-b border-slate-50 last:border-0">
                  <td className="px-4 py-3 font-medium text-slate-800 select-text cursor-text">{t(`pregnancy.${p.result}`)}</td>
                  <Td>{p.checked_at}</Td><Td>{p.due_date ?? '—'}</Td><Td>{p.notes || '—'}</Td>
                  <RowActions
                    onEdit={() => setModal({ kind: 'pregnancy', initial: p })}
                    onDelete={() => setModal({ kind: 'delete', recordType: 'pregnancies', id: p.id })}
                  />
                </tr>
              ))}</tbody>
            </table>
          )
        )}

        {/* TB Tests */}
        {tab === 'tb_tests' && (
          tb_tests.length === 0 ? <Empty t={t} /> : (
            <table className="w-full text-sm">
              <thead><tr className="border-b border-slate-100 bg-slate-50">
                <Th>{t('tb_test.result')}</Th><Th>{t('tb_test.tested_at')}</Th>
                <Th>{t('tb_test.notes')}</Th><th />
              </tr></thead>
              <tbody>{tb_tests.map(tb => (
                <tr key={tb.id} className="border-b border-slate-50 last:border-0">
                  <td className="px-4 py-3 font-medium text-slate-800 select-text cursor-text">{t(`tb_test.${tb.result}`)}</td>
                  <Td>{tb.tested_at}</Td><Td>{tb.notes || '—'}</Td>
                  <RowActions
                    onEdit={() => setModal({ kind: 'tb_test', initial: tb })}
                    onDelete={() => setModal({ kind: 'delete', recordType: 'tb_tests', id: tb.id })}
                  />
                </tr>
              ))}</tbody>
            </table>
          )
        )}

        {/* Weights */}
        {tab === 'weights' && (
          weights.length === 0 ? <Empty t={t} /> : (
            <table className="w-full text-sm">
              <thead><tr className="border-b border-slate-100 bg-slate-50">
                <Th>{t('weight.weight_kg')}</Th><Th>{t('weight.weighed_at')}</Th>
                <Th>{t('weight.notes')}</Th><th />
              </tr></thead>
              <tbody>{weights.map(w => (
                <tr key={w.id} className="border-b border-slate-50 last:border-0">
                  <td className="px-4 py-3 font-medium text-slate-800 select-text cursor-text">{w.weight_kg} kg</td>
                  <Td>{w.weighed_at}</Td><Td>{w.notes || '—'}</Td>
                  <RowActions
                    onEdit={() => setModal({ kind: 'weight', initial: w })}
                    onDelete={() => setModal({ kind: 'delete', recordType: 'weights', id: w.id })}
                  />
                </tr>
              ))}</tbody>
            </table>
          )
        )}
      </div>

      {/* Modals */}
      {modal?.kind === 'edit_animal' && (
        <EditAnimalModal profile={profile} onClose={() => setModal(null)} onSaved={p => { setProfile(p); setModal(null) }} />
      )}
      {modal?.kind === 'vaccination' && (
        <VaccinationModal animalId={profile.id} initial={modal.initial} onClose={() => setModal(null)}
          onSaved={(v, isEdit) => { isEdit ? replaceVax(v) : addVax(v); setModal(null) }} />
      )}
      {modal?.kind === 'pregnancy' && (
        <PregnancyModal animalId={profile.id} initial={modal.initial} onClose={() => setModal(null)}
          onSaved={(p, isEdit) => { isEdit ? replacePreg(p) : addPreg(p); setModal(null) }} />
      )}
      {modal?.kind === 'tb_test' && (
        <TbTestModal animalId={profile.id} initial={modal.initial} onClose={() => setModal(null)}
          onSaved={(tb, isEdit) => { isEdit ? replaceTb(tb) : addTb(tb); setModal(null) }} />
      )}
      {modal?.kind === 'weight' && (
        <WeightModal animalId={profile.id} initial={modal.initial} onClose={() => setModal(null)}
          onSaved={(w, isEdit) => { isEdit ? replaceWeight(w) : addWeight(w); setModal(null) }} />
      )}
      {modal?.kind === 'remove' && (
        <RemoveModal animalId={profile.id} onClose={() => setModal(null)} onDone={() => { setModal(null); load() }} />
      )}
      {modal?.kind === 'edit_removal' && removal && (
        <EditRemovalModal animalId={profile.id} current={removal} onClose={() => setModal(null)}
          onDone={r => { setProfile(p => p ? { ...p, removal: r } : p); setModal(null) }} />
      )}
      {modal?.kind === 'restore' && (
        <RestoreModal animalId={profile.id} onClose={() => setModal(null)} onDone={() => { setModal(null); load() }} />
      )}
      {modal?.kind === 'delete' && (
        <DeleteConfirmModal
          onClose={() => setModal(null)}
          onConfirm={() => deleteRecord(modal.recordType, modal.id)}
        />
      )}
    </div>
  )
}
