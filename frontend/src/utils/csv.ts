import { invoke } from '@tauri-apps/api/core'

// RFC 4180-ish CSV: quote any field containing a comma, quote, or newline,
// doubling embedded quotes. Values are stringified as-is (no locale number
// formatting), so exported files stay portable between EN/ES installs.
function csvField(value: string | number | null | undefined): string {
  const s = value == null ? '' : String(value)
  return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s
}

export function toCsv(headers: string[], rows: (string | number | null | undefined)[][]): string {
  const lines = [headers.map(csvField).join(',')]
  for (const row of rows) lines.push(row.map(csvField).join(','))
  return lines.join('\r\n')
}

// e.g. "pregnancy_report" -> "pregnancy_report_2026-08-19_1432.csv".
// No colons/spaces, so it's a valid filename on every OS as-is.
export function timestampedFilename(base: string): string {
  const d = new Date()
  const pad = (n: number) => String(n).padStart(2, '0')
  const stamp = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}_${pad(d.getHours())}${pad(d.getMinutes())}`
  return `${base}_${stamp}.csv`
}

// Prompts the user with a native "Save As" dialog (pre-filled with
// `filename`) via the export_csv Tauri command, and writes the CSV there.
// Falls back to a plain browser download (no location picker — browsers
// don't expose one without extra permissions) when running outside Tauri,
// e.g. `npm run dev` against a bare browser tab.
// A UTF-8 BOM is prepended so Excel — which otherwise guesses Latin-1 —
// renders accented Spanish text correctly.
export async function downloadCsv(filename: string, csv: string): Promise<void> {
  const bytes = new TextEncoder().encode('﻿' + csv)

  try {
    await invoke<string | null>('export_csv', { filename, data: Array.from(bytes) })
    return // saved, or the user cancelled the dialog — either way, done
  } catch {
    // Not running under Tauri (or the command failed) — fall back to a
    // normal browser download.
  }

  const blob = new Blob([bytes], { type: 'text/csv;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  a.remove()
  URL.revokeObjectURL(url)
}
