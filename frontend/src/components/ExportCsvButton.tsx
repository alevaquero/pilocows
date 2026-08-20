import { useTranslation } from 'react-i18next'
import { toCsv, downloadCsv, timestampedFilename } from '../utils/csv'

function DownloadIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
      <polyline points="7 10 12 15 17 10" />
      <line x1="12" y1="15" x2="12" y2="3" />
    </svg>
  )
}

export default function ExportCsvButton({
  baseName, headers, rows, className = '',
}: {
  baseName: string // e.g. "pregnancy_report" — timestamp + .csv are appended
  headers: string[]
  rows: (string | number | null | undefined)[][]
  className?: string
}) {
  const { t } = useTranslation()
  return (
    <button
      onClick={() => downloadCsv(timestampedFilename(baseName), toCsv(headers, rows)).catch(() => {})}
      title={t('common.export_csv')}
      className={`print:hidden p-1.5 border border-slate-200 rounded-lg text-slate-500 hover:bg-slate-50 hover:text-slate-700 transition-colors ${className}`}
    >
      <DownloadIcon />
    </button>
  )
}
