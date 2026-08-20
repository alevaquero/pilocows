import { useRef, useState } from 'react'

// Voice note play button (native <audio>, fetched lazily via src). Shared by
// SessionDetailPage (per-record / session note audio) and AnimalDetailPage
// (General-session scan audio) — same backend route shape either way:
// GET /sessions/:id/audio or /sessions/:id/records/:eid/audio.
export default function AudioPlayButton({ src, onRowClick }: { src: string; onRowClick?: boolean }) {
  const audioRef = useRef<HTMLAudioElement | null>(null)
  const [playing, setPlaying] = useState(false)

  const toggle = (e: React.MouseEvent) => {
    if (onRowClick) e.stopPropagation() // don't trigger the row's onClick navigation
    const audio = audioRef.current
    if (!audio) return
    if (playing) audio.pause()
    else audio.play().catch(() => {})
  }

  return (
    <>
      <button
        onClick={toggle}
        className="print:hidden inline-flex items-center justify-center w-6 h-6 rounded-full bg-slate-100 text-slate-600 hover:bg-slate-200 shrink-0"
        title={playing ? 'Pause' : 'Play'}
      >
        {playing ? '⏸' : '▶'}
      </button>
      <audio
        ref={audioRef}
        src={src}
        preload="none"
        onPlay={() => setPlaying(true)}
        onPause={() => setPlaying(false)}
        onEnded={() => setPlaying(false)}
        className="hidden"
      />
    </>
  )
}
