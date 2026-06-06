import { useState } from 'react'
import { NavLink, Outlet } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
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

export default function Layout() {
  const { t } = useTranslation()
  const [showDebugLog, setShowDebugLog] = useState(false)

  return (
    <div className="flex h-screen bg-slate-100 overflow-hidden print:block print:h-auto print:overflow-visible print:bg-white">
      {/* Sidebar */}
      <aside className="w-52 bg-slate-800 flex flex-col shrink-0 print:hidden">
        <div className="px-5 py-5 border-b border-slate-700">
          <h1 className="text-white font-bold text-lg tracking-tight">Pilocows</h1>
          <p className="text-slate-400 text-xs mt-0.5">Pilo's Farm</p>
        </div>

        <nav className="flex-1 px-3 py-4 space-y-1">
          <NavItem to="/animals" label={t('nav.animals')} />
          <NavItem to="/tags" label={t('nav.tags')} />
          <NavItem to="/sessions" label={t('nav.sessions')} />
          <NavItem to="/reports" label={t('nav.reports')} />
          <NavItem to="/sync" label={t('nav.sync')} />
          <NavItem to="/settings" label={t('nav.settings')} />
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

      </aside>

      {/* Right column: main content + debug panel stacked */}
      <div className="flex-1 flex flex-col overflow-hidden min-w-0 print:block print:overflow-visible">
        <main className="flex-1 overflow-auto print:overflow-visible">
          <Outlet />
        </main>

        {showDebugLog && (
          <div className="print:hidden">
            <DebugLogPanel onClose={() => setShowDebugLog(false)} />
          </div>
        )}
      </div>
    </div>
  )
}
