import { useState } from 'react'
import { NavLink, Outlet } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import i18n from '../i18n/index'
import DebugLogPanel from './DebugLogPanel'

function NavItem({ to, label }: { to: string; label: string }) {
  return (
    <NavLink
      to={to}
      className={({ isActive }) =>
        `block px-4 py-2.5 rounded-lg text-sm font-medium transition-colors ${
          isActive
            ? 'bg-white/20 text-white'
            : 'text-slate-300 hover:bg-white/10 hover:text-white'
        }`
      }
    >
      {label}
    </NavLink>
  )
}

function LanguageToggle() {
  const lang = i18n.language.startsWith('es') ? 'es' : 'en'
  const toggle = () => {
    const next = lang === 'es' ? 'en' : 'es'
    i18n.changeLanguage(next)
    localStorage.setItem('lang', next)
  }
  return (
    <button
      onClick={toggle}
      className="flex items-center gap-2 px-4 py-2 rounded-lg text-slate-400 hover:text-white hover:bg-white/10 text-sm transition-colors"
    >
      <span className="font-semibold">{lang.toUpperCase()}</span>
      <span className="text-slate-500">→</span>
      <span>{lang === 'es' ? 'EN' : 'ES'}</span>
    </button>
  )
}

export default function Layout() {
  const { t } = useTranslation()
  const [showDebugLog, setShowDebugLog] = useState(false)

  return (
    <div className="flex h-screen bg-slate-100 overflow-hidden">
      {/* Sidebar */}
      <aside className="w-52 bg-slate-800 flex flex-col shrink-0">
        <div className="px-5 py-5 border-b border-slate-700">
          <h1 className="text-white font-bold text-lg tracking-tight">Pilocows</h1>
          <p className="text-slate-400 text-xs mt-0.5">Pilo's Farm</p>
        </div>

        <nav className="flex-1 px-3 py-4 space-y-1">
          <NavItem to="/animals" label={t('nav.animals')} />
          <NavItem to="/tags" label={t('nav.tags')} />
          <NavItem to="/sessions" label={t('nav.sessions')} />
          <NavItem to="/sync" label={t('nav.sync')} />
        </nav>

        {/* View section */}
        <div className="px-3 py-3 border-t border-slate-700">
          <button
            onClick={() => setShowDebugLog(v => !v)}
            className={`flex items-center gap-2 w-full px-4 py-2 rounded-lg text-sm font-medium transition-colors ${
              showDebugLog
                ? 'bg-white/20 text-white'
                : 'text-slate-400 hover:bg-white/10 hover:text-white'
            }`}
          >
            <span className="font-mono text-xs leading-none">{'</>'}</span>
            Debug Log
          </button>
        </div>

        <div className="px-3 py-4 border-t border-slate-700">
          <LanguageToggle />
        </div>
      </aside>

      {/* Right column: main content + debug panel stacked */}
      <div className="flex-1 flex flex-col overflow-hidden min-w-0">
        <main className="flex-1 overflow-auto">
          <Outlet />
        </main>

        {showDebugLog && (
          <DebugLogPanel onClose={() => setShowDebugLog(false)} />
        )}
      </div>
    </div>
  )
}
