import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { reportsApi, type HerdRow } from '../../api/reports'
import { CATEGORIES } from '../../api/animals'
import PrintButton from '../../components/PrintButton'

type GainFilter = '' | 'gained' | 'lost'
type SortKey = 'weight_desc' | 'weight_asc' | 'gain_desc' | 'gain_asc'

export default function WeightProgressReport() {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const [allRows, setAllRows] = useState<HerdRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [category, setCategory] = useState('')
  const [minWeight, setMinWeight] = useState('')
  const [maxWeight, setMaxWeight] = useState('')
  const [since, setSince] = useState('')
  const [gainFilter, setGainFilter] = useState<GainFilter>('')
  const [sort, setSort] = useState<SortKey>('weight_desc')

  useEffect(() => {
    setLoading(true)
    reportsApi.herd()
      .then(setAllRows)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  const rows = allRows
    .filter(r => {
      if (r.last_weight_kg == null) return false
      if (category && r.category !== category) return false
      if (minWeight && r.last_weight_kg < Number(minWeight)) return false
      if (maxWeight && r.last_weight_kg > Number(maxWeight)) return false
      if (since && r.last_weighed_at && r.last_weighed_at < since) return false
      if (gainFilter) {
        const gain = r.prev_weight_kg != null ? r.last_weight_kg - r.prev_weight_kg : null
        if (gainFilter === 'gained' && (gain == null || gain <= 0)) return false
        if (gainFilter === 'lost' && (gain == null || gain >= 0)) return false
      }
      return true
    })
    .sort((a, b) => {
      const gainA = a.prev_weight_kg != null ? a.last_weight_kg! - a.prev_weight_kg : null
      const gainB = b.prev_weight_kg != null ? b.last_weight_kg! - b.prev_weight_kg : null
      switch (sort) {
        case 'weight_desc': return (b.last_weight_kg ?? 0) - (a.last_weight_kg ?? 0)
        case 'weight_asc':  return (a.last_weight_kg ?? 0) - (b.last_weight_kg ?? 0)
        case 'gain_desc':   return (gainB ?? -Infinity) - (gainA ?? -Infinity)
        case 'gain_asc':    return (gainA ?? Infinity) - (gainB ?? Infinity)
      }
    })

  return (
    <div className="p-8">
      <div className="flex items-center gap-4 mb-4">
        <button onClick={() => navigate('/reports')} className="print:hidden text-sm text-slate-500 hover:text-slate-800">
          ← {t('reports.title')}
        </button>
        <h2 className="text-xl font-semibold text-slate-800">{t('reports.weights.title')}</h2>
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
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <input
            type="number"
            value={minWeight}
            onChange={e => setMinWeight(e.target.value)}
            placeholder={t('reports.weights.min_weight')}
            className="w-24 border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
          <span>–</span>
          <input
            type="number"
            value={maxWeight}
            onChange={e => setMaxWeight(e.target.value)}
            placeholder={t('reports.weights.max_weight')}
            className="w-24 border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
        </div>
        <div className="flex items-center gap-2 text-sm text-slate-600">
          <span>{t('reports.weights.since')}</span>
          <input
            type="date"
            value={since}
            onChange={e => setSince(e.target.value)}
            className="border border-slate-200 rounded-lg px-2 py-1.5 bg-white text-slate-700 text-sm"
          />
        </div>
        <select
          value={gainFilter}
          onChange={e => setGainFilter(e.target.value as GainFilter)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="">{t('reports.weights.all_changes')}</option>
          <option value="gained">{t('reports.weights.gained')}</option>
          <option value="lost">{t('reports.weights.lost')}</option>
        </select>
        <select
          value={sort}
          onChange={e => setSort(e.target.value as SortKey)}
          className="text-sm border border-slate-200 rounded-lg px-3 py-1.5 bg-white text-slate-700"
        >
          <option value="weight_desc">{t('reports.weights.sort_weight_desc')}</option>
          <option value="weight_asc">{t('reports.weights.sort_weight_asc')}</option>
          <option value="gain_desc">{t('reports.weights.sort_gain_desc')}</option>
          <option value="gain_asc">{t('reports.weights.sort_gain_asc')}</option>
        </select>
      </div>

      {error && <p className="mb-4 text-sm text-red-600 bg-red-50 rounded-xl px-4 py-3">{error}</p>}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : rows.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('reports.weights.no_data')}</p>
      ) : (
        <div className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-100 bg-slate-50">
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">EID</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('animals.category')}</th>
                <th className="text-right px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.weights.current')}</th>
                <th className="text-right px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.weights.change')}</th>
                <th className="text-right px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.weights.previous')}</th>
                <th className="text-left px-4 py-3 text-xs font-semibold text-slate-500 uppercase tracking-wide">{t('reports.weights.date')}</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(row => {
                const gain = row.prev_weight_kg != null
                  ? +(row.last_weight_kg! - row.prev_weight_kg).toFixed(1)
                  : null
                return (
                  <tr
                    key={row.id}
                    className="border-b border-slate-50 last:border-0 hover:bg-slate-50 cursor-pointer"
                    onClick={() => navigate(`/animals/${row.id}`)}
                  >
                    <td className="px-4 py-3 font-mono text-slate-800">{row.tag_number}</td>
                    <td className="px-4 py-3 text-slate-600">{t(`animals.${row.category}`)}</td>
                    <td className="px-4 py-3 text-right font-medium text-slate-800">{row.last_weight_kg} kg</td>
                    <td className="px-4 py-3 text-right">
                      {gain != null ? (
                        <span className={gain >= 0 ? 'text-green-600 font-medium' : 'text-red-500 font-medium'}>
                          {gain >= 0 ? '+' : ''}{gain} kg
                        </span>
                      ) : <span className="text-slate-300">—</span>}
                    </td>
                    <td className="px-4 py-3 text-right text-slate-500">
                      {row.prev_weight_kg != null ? `${row.prev_weight_kg} kg` : <span className="text-slate-300">—</span>}
                    </td>
                    <td className="px-4 py-3 text-slate-500">{row.last_weighed_at}</td>
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
