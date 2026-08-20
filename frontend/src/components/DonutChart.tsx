import { useState } from 'react'

export interface DonutSegment {
  key: string
  label: string
  value: number
  color: string
}

const SIZE = 160
const STROKE = 26
const HOVER_GROWTH = 4 // extra stroke-width on hover — sized into R below so it can't clip
const R = (SIZE - STROKE - HOVER_GROWTH) / 2
const CIRC = 2 * Math.PI * R
const GAP_PX = 3 // surface gap between adjacent arcs, per the mark spec

// Part-to-whole donut for a single categorical breakdown (≤ ~6-8 segments).
// Always pairs with a legend of count + percentage per segment — the direct
// labels double as the accessible "table view" (no separate toggle needed
// since nothing is hidden behind hover).
export default function DonutChart({ segments, total }: { segments: DonutSegment[]; total: number }) {
  const [hovered, setHovered] = useState<string | null>(null)

  let cumulative = 0
  const arcs = segments.map(s => {
    const frac = total > 0 ? s.value / total : 0
    const len = frac * CIRC
    const dash = Math.max(len - GAP_PX, 0)
    const offset = -cumulative
    cumulative += len
    return { ...s, dash, offset, frac }
  })

  return (
    <div className="flex items-center gap-6 flex-wrap">
      <svg
        width={SIZE}
        height={SIZE}
        viewBox={`0 0 ${SIZE} ${SIZE}`}
        role="img"
        aria-label="Pregnancy result distribution"
        className="shrink-0"
      >
        <g transform={`rotate(-90 ${SIZE / 2} ${SIZE / 2})`}>
          <circle
            cx={SIZE / 2} cy={SIZE / 2} r={R}
            fill="none" stroke="#e1e0d9" strokeWidth={STROKE}
          />
          {arcs.filter(a => a.dash > 0).map(a => (
            <circle
              key={a.key}
              cx={SIZE / 2} cy={SIZE / 2} r={R}
              fill="none"
              stroke={a.color}
              strokeWidth={hovered === a.key ? STROKE + HOVER_GROWTH : STROKE}
              strokeDasharray={`${a.dash} ${CIRC - a.dash}`}
              strokeDashoffset={a.offset}
              strokeLinecap="butt"
              onMouseEnter={() => setHovered(a.key)}
              onMouseLeave={() => setHovered(null)}
              className="transition-[stroke-width] cursor-default"
            >
              <title>{`${a.label}: ${a.value} (${(a.frac * 100).toFixed(1)}%)`}</title>
            </circle>
          ))}
        </g>
        <text
          x={SIZE / 2} y={SIZE / 2 - 6}
          textAnchor="middle"
          className="fill-slate-800"
          style={{ font: '600 22px system-ui, -apple-system, sans-serif' }}
        >
          {total}
        </text>
        <text
          x={SIZE / 2} y={SIZE / 2 + 14}
          textAnchor="middle"
          className="fill-slate-400"
          style={{ font: '400 11px system-ui, -apple-system, sans-serif' }}
        >
          total
        </text>
      </svg>

      <ul className="text-sm min-w-[220px]">
        {segments.map(s => {
          const pct = total > 0 ? (s.value / total) * 100 : 0
          return (
            <li
              key={s.key}
              onMouseEnter={() => setHovered(s.key)}
              onMouseLeave={() => setHovered(null)}
              className={`flex items-center gap-2 py-1 px-1.5 rounded-md ${hovered === s.key ? 'bg-slate-50' : ''}`}
            >
              <span className="w-2.5 h-2.5 rounded-full shrink-0" style={{ backgroundColor: s.color }} />
              <span className="text-slate-600 flex-1">{s.label}</span>
              <span className="text-slate-800 font-medium tabular-nums">{s.value}</span>
              <span className="text-slate-400 text-xs tabular-nums w-12 text-right">{pct.toFixed(1)}%</span>
            </li>
          )
        })}
      </ul>
    </div>
  )
}
