import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow } from '../../api/reports'
import { CATEGORIES } from '../../api/animals'
import { TB_RESULTS } from '../../api/health'
import PrintButton from '../../components/PrintButton'
import ExportCsvButton from '../../components/ExportCsvButton'
import DonutChart from '../../components/DonutChart'

// Test result is a health status (negative = healthy, positive = disease
// detected), not an arbitrary identity — so it uses the reserved status
// colors (matching the green/red/gray already used for TestBadge elsewhere
// in the app, e.g. HerdStatusReport), not the categorical palette.
const RESULT_COLORS: Record<string, string> = {
  negative:     '#0ca30c', // status good
  positive:     '#d03b3b', // status critical
  inconclusive: '#898781', // muted/neutral — no result either way
}

export default function TestReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [category, setCategory] = useState('')
  const [resultFilter, setResultFilter] = useState('')
  const [dateFrom, setDateFrom] = useState('')
  const [dateTo, setDateTo] = useState('')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd()
      .then(setAllRows)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows
    .filter(r => {
      if (r.last_test_result == null) return false
      if (category && r.category !== category) return false
      if (resultFilter && r.last_test_result !== resultFilter) return false
      if (dateFrom && r.last_tested_at && r.last_tested_at < dateFrom) return false
      if (dateTo && r.last_tested_at && r.last_tested_at > dateTo) return false
      return true
    })
    .sort((a, b) => (b.last_tested_at ?? '').localeCompare(a.last_tested_at ?? ''))

  const resultCounts = TB_RESULTS.map(key => ({
    key,
    label: t(`test.${key}`),
    value: rows.filter(r => r.last_test_result === key).length,
    color: RESULT_COLORS[key],
  }))

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.tests.title')}</h2>
        <div className="ml-auto flex items-center gap-2">
          <ExportCsvButton
            baseName="test_report"
            headers={['EID', t('animals.category'), t('reports.tests.test_name'), t('reports.tests.result'), t('reports.tests.date')]}
            rows={rows.map(row => [
              row.tag_number,
              t(`animals.${row.category}`),
              row.last_test_name ?? '',
              t(`test.${row.last_test_result}`),
              row.last_tested_at ?? '',
            ])}
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
        <select
          value={resultFilter}
          onChange={e => setResultFilter(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('reports.tests.result_all')}</option>
          {TB_RESULTS.map(r => (
            <option key={r} value={r}>{t(`test.${r}`)}</option>
          ))}
        </select>
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <span>{t('reports.tests.date_from')}</span>
          <input
            type="date"
            value={dateFrom}
            onChange={e => setDateFrom(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
          <span>{t('reports.tests.date_to')}</span>
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
        <p className="text-slate-500 text-sm">{t('reports.tests.no_data')}</p>
      ) : (
        <>
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 p-5 mb-4">
          <h3 className="text-sm font-semibold text-slate-600 uppercase tracking-wide mb-4">
            {t('reports.tests.result_breakdown')}
          </h3>
          <DonutChart segments={resultCounts} total={rows.length} />
        </div>
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.tests.test_name')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.tests.result')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.tests.date')}</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(row => {
                const resultCls = row.last_test_result === 'negative' ? 'bg-green-100 text-green-700'
                  : row.last_test_result === 'positive' ? 'bg-red-100 text-red-700'
                  : 'bg-slate-100 text-slate-600'
                return (
                  <tr
                    key={row.id}
                    className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                    onClick={() => navigate(`/animals/${row.id}`)}
                  >
                    <td className="px-4 py-3 font-mono text-slate-800">{row.tag_number}</td>
                    <td className="px-4 py-3 text-slate-600">{t(`animals.${row.category}`)}</td>
                    <td className="px-4 py-3 text-slate-700">{row.last_test_name || '—'}</td>
                    <td className="px-4 py-3">
                      <span className={`px-2 py-0.5 rounded text-xs font-medium ${resultCls}`}>
                        {t(`test.${row.last_test_result}`)}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-slate-600">{row.last_tested_at ?? '—'}</td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
        </>
      )}
    </div>
  )
}
