import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow } from '../../api/reports'
import { CATEGORIES, breedLabel } from '../../api/animals'
import PrintButton from '../../components/PrintButton'

function TestBadge({ result }: { result: string }) {
  const { t } = useTranslation()
  const cls = result === 'negative' ? 'bg-green-100 text-green-700'
    : result === 'positive' ? 'bg-red-100 text-red-700'
    : 'bg-slate-100 text-slate-600'
  return <span className={`px-2 py-0.5 rounded text-xs font-medium ${cls}`}>{t(`test.${result}`)}</span>
}

function PregnancyBadge({ result }: { result: string }) {
  const { t } = useTranslation()
  const isPreg = result.includes('pregnant') && result !== 'not_pregnant'
  const cls = isPreg ? 'bg-pink-100 text-pink-700' : 'bg-slate-100 text-slate-600'
  return <span className={`px-2 py-0.5 rounded text-xs font-medium ${cls}`}>{t(`pregnancy.${result}`)}</span>
}

export default function HerdStatusReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [category, setCategory] = useState('')
  const [breed, setBreed] = useState('')
  const [vaccFilter, setVaccFilter] = useState('')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd()
      .then(setAllRows)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows.filter(r => {
    if (category && r.category !== category) return false
    if (breed && r.breed !== breed) return false
    if (vaccFilter === 'overdue' && r.overdue_count === 0) return false
    if (vaccFilter === 'ok' && (r.overdue_count > 0 || r.next_vaccine_due_at == null)) return false
    if (vaccFilter === 'none' && (r.next_vaccine_due_at != null || r.overdue_count > 0)) return false
    return true
  })

  const availableBreeds = [...new Set(allRows.map(r => r.breed).filter(Boolean))] as string[]

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.herd.title')}</h2>
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
        {availableBreeds.length > 0 && (
          <select
            value={breed}
            onChange={e => setBreed(e.target.value)}
            className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
          >
            <option value="">{t('reports.herd.breed_all')}</option>
            {availableBreeds.map(b => <option key={b} value={b}>{breedLabel(t, b)}</option>)}
          </select>
        )}
        <select
          value={vaccFilter}
          onChange={e => setVaccFilter(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('reports.herd.vacc_all')}</option>
          <option value="overdue">{t('reports.herd.overdue')}</option>
          <option value="ok">{t('reports.herd.vacc_ok')}</option>
          <option value="none">{t('reports.herd.vacc_none')}</option>
        </select>
      </div>

      {error && <p className="mb-4 text-sm text-red-600 bg-red-50 rounded-xl px-4 py-3">{error}</p>}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : rows.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('animals.no_animals')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-x-auto">
          <table className="w-full text-sm whitespace-nowrap">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.herd.weight')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.herd.change')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.herd.pregnancy')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.herd.last_test')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.herd.next_vacc')}</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(row => {
                const gain = row.last_weight_kg != null && row.prev_weight_kg != null
                  ? +(row.last_weight_kg - row.prev_weight_kg).toFixed(1)
                  : null
                return (
                  <tr
                    key={row.id}
                    className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                    onClick={() => navigate(`/animals/${row.id}`)}
                  >
                    <td className="px-4 py-3 font-mono text-slate-800">{row.tag_number}</td>
                    <td className="px-4 py-3 text-slate-600">{t(`animals.${row.category}`)}</td>
                    <td className="px-4 py-3 text-slate-700">
                      {row.last_weight_kg != null ? `${row.last_weight_kg} kg` : <span className="text-slate-300">—</span>}
                    </td>
                    <td className="px-4 py-3">
                      {gain != null ? (
                        <span className={gain >= 0 ? 'text-green-600' : 'text-red-500'}>
                          {gain >= 0 ? '+' : ''}{gain} kg
                        </span>
                      ) : <span className="text-slate-300">—</span>}
                    </td>
                    <td className="px-4 py-3">
                      {row.last_pregnancy_result
                        ? <PregnancyBadge result={row.last_pregnancy_result} />
                        : <span className="text-slate-300">—</span>}
                    </td>
                    <td className="px-4 py-3">
                      {row.last_test_result
                        ? <span className="flex items-center gap-1">
                            <TestBadge result={row.last_test_result} />
                            {row.last_test_name && <span className="text-slate-400 text-xs">{row.last_test_name}</span>}
                          </span>
                        : <span className="text-slate-300">—</span>}
                    </td>
                    <td className="px-4 py-3">
                      {row.overdue_count > 0 ? (
                        <span className="text-xs font-medium text-red-600 bg-red-50 px-2 py-0.5 rounded">
                          {row.overdue_count} {t('reports.herd.overdue')}
                        </span>
                      ) : row.next_vaccine_due_at ? (
                        <span className="text-xs text-slate-500">{row.next_vaccine} · {row.next_vaccine_due_at}</span>
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
