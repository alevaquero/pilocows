import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow } from '../../api/reports'
import { CATEGORIES } from '../../api/animals'
import PrintButton from '../../components/PrintButton'
import ExportCsvButton from '../../components/ExportCsvButton'
import DonutChart from '../../components/DonutChart'

type Completeness = 'both_known' | 'father_only' | 'mother_only' | 'neither_known'

function completeness(row: HerdRow): Completeness {
  if (row.father_tag_number && row.mother_tag_number) return 'both_known'
  if (row.father_tag_number) return 'father_only'
  if (row.mother_tag_number) return 'mother_only'
  return 'neither_known'
}

// Categorical, not status — "missing a parent" is a data-completeness state,
// not a good/bad health signal, so this uses the dataviz skill's ordinary
// categorical order (fixed regardless of which categories are present in
// the current filter) rather than the reserved status colors.
const COMPLETENESS_COLORS: Record<Completeness, string> = {
  both_known:    '#2a78d6', // blue
  father_only:   '#eb6834', // orange
  mother_only:   '#1baf7a', // aqua
  neither_known: '#eda100', // yellow
}
const COMPLETENESS_ORDER: Completeness[] = ['both_known', 'father_only', 'mother_only', 'neither_known']

export default function LineageReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [category, setCategory] = useState('')
  const [sex, setSex] = useState('')
  const [completenessFilter, setCompletenessFilter] = useState('')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd()
      .then(setAllRows)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows.filter(r => {
    if (category && r.category !== category) return false
    if (sex && r.sex !== sex) return false
    const c = completeness(r)
    if (completenessFilter === 'both' && c !== 'both_known') return false
    if (completenessFilter === 'missing' && c === 'both_known') return false
    return true
  })

  const completenessCounts = COMPLETENESS_ORDER.map(key => ({
    key,
    label: t(`reports.lineage.${key}`),
    value: rows.filter(r => completeness(r) === key).length,
    color: COMPLETENESS_COLORS[key],
  }))

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.lineage.title')}</h2>
        <div className="ml-auto flex items-center gap-2">
          <ExportCsvButton
            baseName="lineage_report"
            headers={['EID', t('animals.category'), t('animals.sex'), t('animals.father'), t('animals.mother'), t('animals.dob')]}
            rows={rows.map(row => [
              row.tag_number,
              t(`animals.${row.category}`),
              t(`animals.${row.sex}`),
              row.father_tag_number ?? '',
              row.mother_tag_number ?? '',
              row.dob ?? '',
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
          value={sex}
          onChange={e => setSex(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('animals.sex')}</option>
          <option value="male">{t('animals.male')}</option>
          <option value="female">{t('animals.female')}</option>
        </select>
        <select
          value={completenessFilter}
          onChange={e => setCompletenessFilter(e.target.value)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('reports.lineage.completeness_all')}</option>
          <option value="both">{t('reports.lineage.completeness_both')}</option>
          <option value="missing">{t('reports.lineage.completeness_missing')}</option>
        </select>
      </div>

      {error && <p className="mb-4 text-sm text-red-600 bg-red-50 rounded-xl px-4 py-3">{error}</p>}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : rows.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('reports.lineage.no_data')}</p>
      ) : (
        <>
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 p-5 mb-4">
          <h3 className="text-sm font-semibold text-slate-600 uppercase tracking-wide mb-4">
            {t('reports.lineage.completeness_breakdown')}
          </h3>
          <DonutChart segments={completenessCounts} total={rows.length} />
        </div>
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.sex')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.father')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.mother')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.dob')}</th>
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
                  <td className="px-4 py-3 text-slate-600">{t(`animals.${row.sex}`)}</td>
                  <td className="px-4 py-3 font-mono text-xs text-slate-500">
                    {row.father_id ? (
                      <button onClick={e => { e.stopPropagation(); navigate(`/animals/${row.father_id}`) }} className="hover:text-slate-800 hover:underline">
                        {row.father_tag_number}
                      </button>
                    ) : <span className="text-slate-300 font-sans">{t('animals.unknown_parent')}</span>}
                  </td>
                  <td className="px-4 py-3 font-mono text-xs text-slate-500">
                    {row.mother_id ? (
                      <button onClick={e => { e.stopPropagation(); navigate(`/animals/${row.mother_id}`) }} className="hover:text-slate-800 hover:underline">
                        {row.mother_tag_number}
                      </button>
                    ) : <span className="text-slate-300 font-sans">{t('animals.unknown_parent')}</span>}
                  </td>
                  <td className="px-4 py-3 text-slate-500">{row.dob ?? '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        </>
      )}
    </div>
  )
}
