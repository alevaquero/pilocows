// Backend timestamps that include a time component are full UTC ISO-8601
// strings (e.g. "2026-08-07T23:25:00Z", from the handheld's RTC via
// backend's unix_to_iso) — rendering that string raw shows UTC, not the
// viewer's own time. Fields that are plain "YYYY-MM-DD" (no time component)
// come from a <input type="date"> in this app's own forms and have no
// timezone attached at all — those must NOT be run through `new Date(...)`,
// since `new Date('YYYY-MM-DD')` parses as UTC midnight and `.toLocaleString`
// can then roll the date back a day in negative-UTC-offset timezones.
export function formatLocalDateTime(value: string | null | undefined): string {
  if (!value) return ''
  if (!value.includes('T')) return value // plain date — nothing to convert
  const d = new Date(value)
  if (isNaN(d.getTime())) return value
  return d.toLocaleString([], { dateStyle: 'medium', timeStyle: 'short' })
}
