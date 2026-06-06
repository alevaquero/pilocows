import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow, daysUntil } from '../../api/reports'
import { CATEGORIES } from '../../api/animals'
import PrintButton from '../../components/PrintButton'

const PREGNANT_RESULTS = ['small_pregnant', 'medium_pregnant', 'big_pregnant']

export default function PregnancyReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [category, setCategory] = useState('')
  const [resultFilter, setResultFilter] = useState('')
  const [checkFrom, setCheckFrom] = useState('')
  const [checkTo, setCheckTo] = useState('')
  const [dueFrom, setDueFrom] = useState('')
  const [dueTo, setDueTo] = useState('')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd()
      .then(setAllRows)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows
    .filter(r => {
      if (r.last_pregnancy_result == null) return false
      if (category && r.category !== category) return false
      if (resultFilter === 'pregnant_any' && !PREGNANT_RESULTS.includes(r.last_pregnancy_result)) return false
      if (resultFilter && resultFilter !== 'pregnant_any' && r.last_pregnancy_result !== resultFilter) return false
      if (checkFrom && r.last_pregnancy_checked_at && r.last_pregnancy_checked_at < checkFrom) return false
      if (checkTo && r.last_pregnancy_checked_at && r.last_pregnancy_checked_at > checkTo) return false
      if (dueFrom && r.last_pregnancy_due_date && r.last_pregnancy_due_date < dueFrom) return false
      if (dueTo && r.last_pregnancy_due_date && r.last_pregnancy_due_date > dueTo) return false
      return true
    })
    .sort((a, b) => {
      const da = a.last_pregnancy_due_date ? daysUntil(a.last_pregnancy_due_date) : Infinity
      const db = b.last_pregnancy_due_date ? daysUntil(b.last_pregnancy_due_date) : Infinity
      return da - db
    })

  const PREGNANCY_RESULTS = ['not_pregnant', 'unknown', 'small_pregnant', 'medium_pregnant', 'big_pregnant', 'rejected']

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.pregnancies.title')}</h2>
        <PrintButton className="ml-auto" />
      </div>

      <div className="print:hidden flex flex-wrap items-center gap-3 mb-6">
        <select
          value={category}
          onChange={e => setCategory(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('animals.all_categories')}</option>
          {CATEGORIES.map(c => <option key={c} value={c}>{t(`animals.${c}`)}</option>)}
        </select>
        <select
          value={resultFilter}
          onChange={e => setResultFilter(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('reports.pregnancies.result_all')}</option>
          <option value="pregnant_any">{t('reports.pregnancies.pregnant_any')}</option>
          {PREGNANCY_RESULTS.map(r => (
            <option key={r} value={r}>{t(`pregnancy.${r}`)}</option>
          ))}
        </select>
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <span>{t('reports.pregnancies.check_from')}</span>
          <input
            type="date"
            value={checkFrom}
            onChange={e => setCheckFrom(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
          <span>{t('reports.pregnancies.check_to')}</span>
          <input
            type="date"
            value={checkTo}
            onChange={e => setCheckTo(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
        </div>
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <span>{t('reports.pregnancies.due_from')}</span>
          <input
            type="date"
            value={dueFrom}
            onChange={e => setDueFrom(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
          <span>{t('reports.pregnancies.due_to')}</span>
          <input
            type="date"
            value={dueTo}
            onChange={e => setDueTo(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
        </div>
      </div>

      {error && <p className="mb-4 text-sm text-red-600 bg-red-50 rounded-xl px-4 py-3">{error}</p>}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : rows.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('reports.pregnancies.no_data')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.pregnancies.result')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.pregnancies.checked')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.pregnancies.due')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.pregnancies.days')}</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(row => {
                const isPreg = row.last_pregnancy_result?.includes('pregnant') && row.last_pregnancy_result !== 'not_pregnant'
                const resultCls = isPreg ? 'bg-pink-100 text-pink-700' : 'bg-slate-100 text-slate-600'
                const days = row.last_pregnancy_due_date ? daysUntil(row.last_pregnancy_due_date) : null
                return (
                  <tr
                    key={row.id}
                    className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                    onClick={() => navigate(`/animals/${row.id}`)}
                  >
                    <td className="px-4 py-3 font-mono text-slate-800">{row.tag_number}</td>
                    <td className="px-4 py-3 text-slate-600">{t(`animals.${row.category}`)}</td>
                    <td className="px-4 py-3">
                      <span className={`px-2 py-0.5 rounded text-xs font-medium ${resultCls}`}>
                        {t(`pregnancy.${row.last_pregnancy_result}`)}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-slate-600">{row.last_pregnancy_checked_at ?? '—'}</td>
                    <td className="px-4 py-3 text-slate-600">{row.last_pregnancy_due_date ?? '—'}</td>
                    <td className="px-4 py-3">
                      {days != null ? (
                        <span className={days < 0 ? 'text-slate-400' : days <= 30 ? 'text-amber-600 font-medium' : 'text-slate-600'}>
                          {days >= 0 ? `${days}d` : `${Math.abs(days)}d ago`}
                        </span>
                      ) : <span className="text-slate-300">—</span>}
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  )
}
