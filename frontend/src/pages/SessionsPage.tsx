import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { useTranslation } from 'react-i18next'
import { sessionsApi, type SessionSummary, sessionTypeKey } from '../api/sessions'

// ── Type badge ────────────────────────────────────────────────────────────────

const TYPE_COLORS: Record<number, string> = {
  0: 'bg-slate-100 text-slate-600',
  1: 'bg-blue-100 text-blue-700',
  2: 'bg-green-100 text-green-700',
  3: 'bg-pink-100 text-pink-700',
  4: 'bg-amber-100 text-amber-700',
}

function TypeBadge({ sessionType }: { sessionType: number }) {
  const { t } = useTranslation()
  const key = `sessions.type_${sessionTypeKey(sessionType)}`
  const color = TYPE_COLORS[sessionType] ?? TYPE_COLORS[0]
  return (
    <span className={`text-xs font-medium px-2 py-0.5 rounded-full ${color}`}>
      {t(key)}
    </span>
  )
}

// ── Page ──────────────────────────────────────────────────────────────────────

export default function SessionsPage() {
  const { t } = useTranslation()
  const navigate = useNavigate()

  const [sessions, setSessions] = useState<SessionSummary[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')

  useEffect(() => {
    sessionsApi.list()
      .then(setSessions)
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false))
  }, [])

  return (
    <div className="p-8">
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-semibold text-slate-800">{t('sessions.title')}</h2>
      </div>

      {error && (
        <div className="mb-6 bg-red-50 border border-red-200 rounded-xl p-4">
          <p className="text-sm text-red-700 select-text cursor-text break-all">{error}</p>
        </div>
      )}

      {loading ? (
        <p className="text-slate-500 text-sm">{t('common.loading')}</p>
      ) : sessions.length === 0 ? (
        <p className="text-slate-500 text-sm">{t('sessions.no_sessions')}</p>
      ) : (
        <div className="space-y-3">
          {sessions.map(s => (
            <SessionCard key={s.id} session={s} onClick={() => navigate(`/sessions/${s.id}`)} />
          ))}
        </div>
      )}
    </div>
  )
}

// ── Session card ──────────────────────────────────────────────────────────────

function SessionCard({ session: s, onClick }: { session: SessionSummary; onClick: () => void }) {
  const { t } = useTranslation()

  const date = new Date(s.created_at).toLocaleDateString()

  return (
    <div
      className="bg-white rounded-xl border border-slate-200 px-4 py-3 cursor-pointer hover:border-slate-300 hover:shadow-sm transition-all"
      onClick={onClick}
    >
      <div className="flex items-start justify-between gap-4">
        <div className="flex-1 min-w-0">
          {/* Name + badges */}
          <div className="flex items-center gap-2 flex-wrap mb-1">
            <span className="text-sm font-semibold text-slate-800 truncate">
              {s.name || '—'}
            </span>
            <TypeBadge sessionType={s.session_type} />
          </div>

          {/* Meta row */}
          <div className="flex items-center gap-3 text-xs text-slate-400 flex-wrap">
            <span>{date}</span>
            <span>·</span>
            <span>{s.record_count} {t('sessions.records')}</span>
            {s.operator && (
              <>
                <span>·</span>
                <span>{s.operator}</span>
              </>
            )}
            {s.farm && (
              <>
                <span>·</span>
                <span>{s.farm}</span>
              </>
            )}
          </div>
        </div>

        {/* Chevron */}
        <span className="text-slate-300 text-lg leading-none mt-0.5">›</span>
      </div>
    </div>
  )
}
