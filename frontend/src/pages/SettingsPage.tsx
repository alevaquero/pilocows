import { useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { invoke } from '@tauri-apps/api/core'
import i18n from '../i18n/index'
import { backupApi } from '../api/backup'

function Section({ title, description, children }: {
  title: string
  description: string
  children: React.ReactNode
}) {
  return (
    <div className="bg-white rounded-xl shadow-sm border border-slate-200 p-6 mb-4">
      <h3 className="font-semibold text-slate-700 mb-1">{title}</h3>
      <p className="text-sm text-slate-500 mb-4">{description}</p>
      {children}
    </div>
  )
}

export default function SettingsPage() {
  const { t } = useTranslation()
  const lang = i18n.language.startsWith('es') ? 'es' : 'en'

  const [backing, setBacking] = useState(false)
  const [backupError, setBackupError] = useState('')
  const [backupSavedPath, setBackupSavedPath] = useState('')

  const [restoreFile, setRestoreFile] = useState<File | null>(null)
  const [restoring, setRestoring] = useState(false)
  const [restoreError, setRestoreError] = useState('')
  const [restoreSuccess, setRestoreSuccess] = useState(false)
  const [confirmingRestore, setConfirmingRestore] = useState(false)
  const fileInputRef = useRef<HTMLInputElement>(null)

  const changeLang = (next: 'en' | 'es') => {
    i18n.changeLanguage(next)
    localStorage.setItem('lang', next)
  }

  const handleBackup = async () => {
    setBackupError('')
    setBackupSavedPath('')
    setBacking(true)
    try {
      const { filename, data } = await backupApi.fetch()
      const savedPath = await invoke<string>('save_backup', { filename, data })
      setBackupSavedPath(savedPath)
    } catch (e) {
      setBackupError(e instanceof Error ? e.message : String(e))
    } finally {
      setBacking(false)
    }
  }

  const handleRestoreConfirm = async () => {
    if (!restoreFile) return
    setConfirmingRestore(false)
    setRestoreError('')
    setRestoreSuccess(false)
    setRestoring(true)
    try {
      await backupApi.restore(restoreFile)
      setRestoreSuccess(true)
      setRestoreFile(null)
      if (fileInputRef.current) fileInputRef.current.value = ''
    } catch (e) {
      setRestoreError(e instanceof Error ? e.message : String(e))
    } finally {
      setRestoring(false)
    }
  }

  const langBtn = (code: 'en' | 'es', label: string) => (
    <button
      onClick={() => changeLang(code)}
      className={`px-5 py-2 rounded-lg text-sm font-medium transition-colors ${
        lang === code
          ? 'bg-slate-800 text-white'
          : 'border border-slate-300 text-slate-600 hover:bg-slate-50'
      }`}
    >
      {label}
    </button>
  )

  return (
    <div className="p-8 max-w-2xl">
      <h2 className="text-xl font-semibold text-slate-800 mb-6">{t('settings.title')}</h2>

      <Section title={t('settings.language')} description={t('settings.language_desc')}>
        <div className="flex gap-2">
          {langBtn('en', 'English')}
          {langBtn('es', 'Español')}
        </div>
      </Section>

      <Section title={t('settings.backup')} description={t('settings.backup_desc')}>
        <button
          onClick={handleBackup}
          disabled={backing}
          className="px-4 py-2 bg-slate-800 text-white text-sm rounded-lg hover:bg-slate-700 disabled:opacity-50"
        >
          {backing ? t('common.loading') : t('settings.backup_btn')}
        </button>
        {backupError && (
          <p className="mt-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{backupError}</p>
        )}
        {backupSavedPath && (
          <p className="mt-3 text-sm text-green-700 bg-green-50 rounded-lg px-3 py-2">
            {t('settings.backup_saved')} <span className="font-mono break-all">{backupSavedPath}</span>
          </p>
        )}
      </Section>

      <Section title={t('settings.restore')} description={t('settings.restore_desc')}>
        <p className="text-sm text-amber-700 bg-amber-50 border border-amber-200 rounded-lg px-3 py-2 mb-4">
          ⚠️ {t('settings.restore_warning')}
        </p>

        <div className="flex items-center gap-3 flex-wrap">
          <label className="px-4 py-2 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50 cursor-pointer">
            {restoreFile ? restoreFile.name : t('settings.restore_pick')}
            <input
              ref={fileInputRef}
              type="file"
              accept=".db"
              className="hidden"
              onChange={e => {
                setRestoreFile(e.target.files?.[0] ?? null)
                setConfirmingRestore(false)
                setRestoreError('')
                setRestoreSuccess(false)
              }}
            />
          </label>

          {!confirmingRestore ? (
            <button
              onClick={() => setConfirmingRestore(true)}
              disabled={!restoreFile || restoring}
              className="px-4 py-2 bg-red-600 text-white text-sm rounded-lg hover:bg-red-700 disabled:opacity-40"
            >
              {restoring ? t('common.loading') : t('settings.restore_btn')}
            </button>
          ) : (
            <div className="flex items-center gap-2">
              <span className="text-sm text-slate-600">{t('settings.restore_confirm_inline')}</span>
              <button
                onClick={handleRestoreConfirm}
                className="px-3 py-1.5 bg-red-600 text-white text-sm rounded-lg hover:bg-red-700"
              >
                {t('common.confirm')}
              </button>
              <button
                onClick={() => setConfirmingRestore(false)}
                className="px-3 py-1.5 text-sm rounded-lg border border-slate-300 text-slate-600 hover:bg-slate-50"
              >
                {t('common.cancel')}
              </button>
            </div>
          )}
        </div>

        {restoreError && (
          <p className="mt-3 text-sm text-red-600 bg-red-50 rounded-lg px-3 py-2">{restoreError}</p>
        )}
        {restoreSuccess && (
          <p className="mt-3 text-sm text-green-700 bg-green-50 rounded-lg px-3 py-2">
            {t('settings.restore_success')}
          </p>
        )}
      </Section>
    </div>
  )
}
