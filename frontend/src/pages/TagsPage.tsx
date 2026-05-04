import { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { tagsApi, type Tag } from '../api/tags'
import Modal, { Field, inputCls } from '../components/Modal'

function AddTagModal({ onClose, onCreated }: { onClose: () => void; onCreated: (t: Tag) => void }) {
  const { t } = useTranslation()
  const [tagNumber, setTagNumber] = useState('')
  const [purchasedAt, setPurchasedAt] = useState(new Date().toISOString().slice(0, 10))
  const [notes, setNotes] = useState('')
  const [error, setError] = useState('')
  const [saving, setSaving] = useState(false)

  const submit = async () => {
    if (!tagNumber.trim()) { setError('Tag number is required'); return }
    setSaving(true)
    try {
      const created = await tagsApi.create({ tag_number: tagNumber.trim(), purchased_at: purchasedAt, notes })
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
      title={t('tags.add')}
      onClose={onClose}
      footer={
        <div className="flex gap-2 justify-end w-full">
          <button onClick={onClose} className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50">
            {t('common.cancel')}
          </button>
          <button
            onClick={submit}
            disabled={saving}
            className="px-4 py-2 text-sm rounded-lg bg-slate-800 text-white hover:bg-slate-700 disabled:opacity-50"
          >
            {saving ? t('common.loading') : t('common.save')}
          </button>
        </div>
      }
    >
      {error && <p className="mb-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{error}</p>}
      <Field label={t('tags.tag_number')}>
        <input className={inputCls} value={tagNumber} onChange={e => setTagNumber(e.target.value)} />
      </Field>
      <Field label={t('tags.purchased_at')}>
        <input type="date" className={inputCls} value={purchasedAt} onChange={e => setPurchasedAt(e.target.value)} />
      </Field>
      <Field label={t('tags.notes')}>
        <textarea className={inputCls} rows={2} value={notes} onChange={e => setNotes(e.target.value)} />
      </Field>
    </Modal>
  )
}

export default function TagsPage() {
  const { t } = useTranslation()
  const [tags, setTags] = useState<Tag[]>([])
  const [unassignedOnly, setUnassignedOnly] = useState(false)
  const [loading, setLoading] = useState(true)
  const [showAdd, setShowAdd] = useState(false)

  const load = useCallback(() => {
    setLoading(true)
    tagsApi.list(unassignedOnly).then(setTags).finally(() => setLoading(false))
  }, [unassignedOnly])

  useEffect(() => { load() }, [load])

  return (
    <div className="p-8">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-semibold text-slate-800">{t('tags.title')}</h2>
        <div className="flex items-center gap-4">
          <label className="flex items-center gap-2 text-sm text-slate-600 cursor-pointer">
            <input
              type="checkbox"
              checked={unassignedOnly}
              onChange={e => setUnassignedOnly(e.target.checked)}
              className="rounded"
            />
            {t('tags.unassigned_only')}
          </label>
          <button
            onClick={() => setShowAdd(true)}
            className="px-4 py-2 bg-slate-800 text-white text-sm rounded-lg hover:bg-slate-700"
          >
            {t('tags.add')}
          </button>
        </div>
      </div>

      {/* Table */}
      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : tags.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('tags.no_tags')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('tags.purchased_at')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('tags.notes')}</th>
              </tr>
            </thead>
            <tbody>
              {tags.map(tag => (
                <tr key={tag.id} className="border-b border-slate-50 last:border-0 hover:bg-slate-50">
                  <td className="px-4 py-3 font-mono text-slate-800">{tag.tag_number}</td>
                  <td className="px-4 py-3 text-slate-600">{tag.purchased_at}</td>
                  <td className="px-4 py-3 text-slate-500">{tag.notes}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {showAdd && (
        <AddTagModal
          onClose={() => setShowAdd(false)}
          onCreated={newTag => setTags(prev => [newTag, ...prev])}
        />
      )}
    </div>
  )
}
