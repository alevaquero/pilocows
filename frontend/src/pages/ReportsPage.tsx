import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'

interface ReportCard {
  path: string
  titleKey: string
  descKey: string
  color: string
}

const REPORTS: ReportCard[] = [
  { path: '/reports/herd',         titleKey: 'reports.herd.title',         descKey: 'reports.herd.desc',         color: 'bg-blue-50 border-blue-200 text-blue-700' },
  { path: '/reports/vaccinations', titleKey: 'reports.vaccinations.title', descKey: 'reports.vaccinations.desc', color: 'bg-amber-50 border-amber-200 text-amber-700' },
  { path: '/reports/pregnancies',  titleKey: 'reports.pregnancies.title',  descKey: 'reports.pregnancies.desc',  color: 'bg-pink-50 border-pink-200 text-pink-700' },
  { path: '/reports/tests',        titleKey: 'reports.tests.title',        descKey: 'reports.tests.desc',        color: 'bg-purple-50 border-purple-200 text-purple-700' },
  { path: '/reports/lineage',      titleKey: 'reports.lineage.title',      descKey: 'reports.lineage.desc',      color: 'bg-indigo-50 border-indigo-200 text-indigo-700' },
  { path: '/reports/weights',      titleKey: 'reports.weights.title',      descKey: 'reports.weights.desc',      color: 'bg-green-50 border-green-200 text-green-700' },
  { path: '/reports/removals',     titleKey: 'reports.removals.title',     descKey: 'reports.removals.desc',     color: 'bg-slate-50 border-slate-200 text-slate-700' },
]

export default function ReportsPage() {
  const { t } = useTranslation()
  const navigate = useNavigate()

  return (
    <div className="p-8">
      <h2 className="text-xl font-semibold text-slate-800 mb-6">{t('reports.title')}</h2>
      <div className="grid grid-cols-1 gap-4 max-w-2xl">
        {REPORTS.map(r => (
          <button
            key={r.path}
            onClick={() => navigate(r.path)}
            className={`text-left border rounded-xl p-5 hover:shadow-sm transition-all ${r.color}`}
          >
            <p className="font-semibold text-base mb-1">{t(r.titleKey)}</p>
            <p className="text-sm opacity-80">{t(r.descKey)}</p>
          </button>
        ))}
      </div>
    </div>
  )
}
