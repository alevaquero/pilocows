import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow } from '../../api/reports'
import { CATEGORIES } from '../../api/animals'
import PrintButton from '../../components/PrintButton'

const REASON_CLS: Record<string, string> = {
  sold:        'bg-amber-100 text-amber-700',
  died:        'bg-red-100 text-red-700',
  slaughtered: 'bg-slate-100 text-slate-700',
  other:       'bg-slate-100 text-slate-500',
}

const REASONS = ['sold', 'died', 'slaughtered', 'other']

export default function RemovalsReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [category, setCategory] = useState('')
  const [reason, setReason] = useState('')
  const [dateFrom, setDateFrom] = useState('')
  const [dateTo, setDateTo] = useState('')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd({ includeInactive: true })
      .then(all => setAllRows(all.filter(r => r.is_active === 0)))
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows.filter(r => {
    if (category && r.category !== category) return false
    if (reason && r.removal_reason !== reason) return false
    if (dateFrom && r.removal_date && r.removal_date < dateFrom) return false
    if (dateTo && r.removal_date && r.removal_date > dateTo) return false
    return true
  })

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.removals.title')}</h2>
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
          value={reason}
          onChange={e => setReason(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('reports.removals.reason_all')}</option>
          {REASONS.map(r => <option key={r} value={r}>{t(`removal.${r}`)}</option>)}
        </select>
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <span>{t('reports.removals.from')}</span>
          <input
            type="date"
            value={dateFrom}
            onChange={e => setDateFrom(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
          <span>{t('reports.removals.to')}</span>
          <input
            type="date"
            value={dateTo}
            onChange={e => setDateTo(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
        </div>
      </div>

      {error && <p className="mb-4 text-sm text-red-600 bg-red-50 rounded-xl px-4 py-3">{error}</p>}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : rows.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('reports.removals.no_data')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.breed')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.removals.reason')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.removals.date')}</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(row => (
                <tr
                  key={row.id}
                  className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                  onClick={() => navigate(`/animals/${row.id}`)}
                >
                  <td className="px-4 py-3 font-mono text-slate-800">{row.tag_number}</td>
                  <td className="px-4 py-3 text-slate-600">{t(`animals.${row.category}`)}</td>
                  <td className="px-4 py-3 text-slate-600">{row.breed || '—'}</td>
                  <td className="px-4 py-3">
                    {row.removal_reason ? (
                      <span className={`px-2 py-0.5 rounded text-xs font-medium ${REASON_CLS[row.removal_reason] ?? REASON_CLS.other}`}>
                        {t(`removal.${row.removal_reason}`)}
                      </span>
                    ) : <span className="text-slate-300">—</span>}
                  </td>
                  <td className="px-4 py-3 text-slate-500">{row.removal_date ?? '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  )
}
