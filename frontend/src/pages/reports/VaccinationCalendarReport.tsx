import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow, daysUntil } from '../../api/reports'
import { CATEGORIES } from '../../api/animals'
import PrintButton from '../../components/PrintButton'
import ExportCsvButton from '../../components/ExportCsvButton'

type VaccStatus = 'overdue' | 'soon' | 'upcoming'

function vaccStatus(dueDateStr: string): VaccStatus {
  const d = daysUntil(dueDateStr)
  if (d < 0) return 'overdue'
  if (d <= 14) return 'soon'
  return 'upcoming'
}

function StatusBadge({ status }: { status: VaccStatus }) {
  const { t } = useTranslation()
  const cfg = {
    overdue:  { cls: 'bg-red-100 text-red-700',    key: 'reports.vaccinations.status_overdue' },
    soon:     { cls: 'bg-amber-100 text-amber-700', key: 'reports.vaccinations.status_soon' },
    upcoming: { cls: 'bg-blue-100 text-blue-700',  key: 'reports.vaccinations.status_upcoming' },
  }[status]
  return <span className={`px-2 py-0.5 rounded text-xs font-medium ${cfg.cls}`}>{t(cfg.key)}</span>
}

const DAY_OPTIONS = [7, 14, 30, 60]

export default function VaccinationCalendarReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [overdueOnly, setOverdueOnly] = useState(false)
  const [daysAhead, setDaysAhead] = useState(30)
  const [category, setCategory] = useState('')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd()
      .then(setAllRows)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows
    .filter(r => {
      if (category && r.category !== category) return false
      if (!r.next_vaccine_due_at && r.overdue_count === 0) return false
      if (overdueOnly) return r.overdue_count > 0
      const d = r.next_vaccine_due_at ? daysUntil(r.next_vaccine_due_at) : -Infinity
      return r.overdue_count > 0 || d <= daysAhead
    })
    .sort((a, b) => {
      const da = a.next_vaccine_due_at ? daysUntil(a.next_vaccine_due_at) : -Infinity
      const db = b.next_vaccine_due_at ? daysUntil(b.next_vaccine_due_at) : -Infinity
      return da - db
    })

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.vaccinations.title')}</h2>
        <div className="ml-auto flex items-center gap-2">
          <ExportCsvButton
            baseName="vaccination_calendar"
            headers={['EID', t('animals.category'), t('reports.vaccinations.vaccine'), t('reports.vaccinations.due'), t('reports.vaccinations.status')]}
            rows={rows.map(row => {
              const status = row.next_vaccine_due_at ? vaccStatus(row.next_vaccine_due_at) : 'overdue'
              return [
                row.tag_number,
                t(`animals.${row.category}`),
                row.next_vaccine ?? (row.overdue_count > 0 ? `${row.overdue_count} venc.` : ''),
                row.next_vaccine_due_at ?? '',
                t(`reports.vaccinations.status_${status}`),
              ]
            })}
          />
          <PrintButton />
        </div>
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
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <span>{t('reports.vaccinations.days_within')}</span>
          <select
            value={overdueOnly ? '' : daysAhead}
            onChange={e => {
              if (e.target.value === '') { setOverdueOnly(false); return }
              setOverdueOnly(false)
              setDaysAhead(Number(e.target.value))
            }}
            disabled={overdueOnly}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 disabled:opacity-40"
          >
            {DAY_OPTIONS.map(d => (
              <option key={d} value={d}>{d} {t('reports.vaccinations.days_label')}</option>
            ))}
            <option value={365}>{t('reports.vaccinations.days_any')}</option>
          </select>
        </div>
        <label className="flex items-center gap-2 text-sm text-slate-600 cursor-pointer">
          <input
            type="checkbox"
            checked={overdueOnly}
            onChange={e => setOverdueOnly(e.target.checked)}
            className="rounded"
          />
          {t('reports.vaccinations.overdue_only')}
        </label>
      </div>

      {error && <p className="mb-4 text-sm text-red-600 bg-red-50 rounded-xl px-4 py-3">{error}</p>}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : rows.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('reports.vaccinations.no_data')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.vaccinations.vaccine')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.vaccinations.due')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.vaccinations.status')}</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(row => {
                const status = row.next_vaccine_due_at ? vaccStatus(row.next_vaccine_due_at) : 'overdue'
                return (
                  <tr
                    key={row.id}
                    className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                    onClick={() => navigate(`/animals/${row.id}`)}
                  >
                    <td className="px-4 py-3 font-mono text-slate-800">{row.tag_number}</td>
                    <td className="px-4 py-3 text-slate-600">{t(`animals.${row.category}`)}</td>
                    <td className="px-4 py-3 text-slate-700">
                      {row.next_vaccine ?? (row.overdue_count > 0 ? `${row.overdue_count} venc.` : '—')}
                    </td>
                    <td className="px-4 py-3 text-slate-600">{row.next_vaccine_due_at ?? '—'}</td>
                    <td className="px-4 py-3"><StatusBadge status={status} /></td>
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
