import { invoke } from '@tauri-apps/api/core'
import { useTranslation } from 'react-i18next'

function PrinterIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polyline points="6 9 6 2 18 2 18 9" />
      <path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2" />
      <rect x="6" y="14" width="12" height="8" />
    </svg>
  )
}

export default function PrintButton({ className = '' }: { className?: string }) {
  const { t } = useTranslation()
  return (
    <button
      onClick={() => invoke('print_window').catch(() => {})}
      title={t('common.print')}
      className={`print:hidden p-1.5 border border-slate-200 rounded-lg text-slate-500 hover:bg-slate-50 hover:text-slate-700 transition-colors ${className}`}
    >
      <PrinterIcon />
    </button>
  )
}
