import { useEffect, useState } from 'react'
import logo from '../assets/pilocows.png'

interface Props {
  onDone: () => void
}

export default function SplashScreen({ onDone }: Props) {
  const [visible, setVisible] = useState(true)

  useEffect(() => {
    // Show for 1.8s, then fade out over 400ms, then unmount
    const fadeTimer = setTimeout(() => setVisible(false), 1800)
    const doneTimer = setTimeout(onDone, 2200)
    return () => { clearTimeout(fadeTimer); clearTimeout(doneTimer) }
  }, [onDone])

  return (
    <div
      className="fixed inset-0 z-50 flex flex-col items-center justify-center bg-slate-800"
      style={{
        transition: 'opacity 400ms ease-out',
        opacity: visible ? 1 : 0,
        pointerEvents: visible ? 'auto' : 'none',
      }}
    >
      <img
        src={logo}
        alt="Pilocows"
        className="w-40 h-40 object-contain mb-6 rounded-2xl shadow-2xl"
      />
      <h1 className="text-white text-3xl font-bold tracking-tight">Pilocows</h1>
      <p className="text-slate-400 text-sm mt-1">Pilo's Farm</p>
    </div>
  )
}
