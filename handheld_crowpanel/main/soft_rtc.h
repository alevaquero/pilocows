#ifndef _SOFT_RTC_H_
#define _SOFT_RTC_H_

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

// Time-keeping entry point for the rest of the app — despite the name, this
// now prefers the real hardware RTC (bsp_rtc, DS3231) when one is wired up
// and answers on the I2C bus, since that keeps ticking on its own coin-cell
// backup power across reboots/power-off. NVS-persisted "last known time" is
// the fallback for boards without a DS3231 attached (or before one's been
// wired in) — still just an approximation, since the system clock resets to
// the epoch on every power cycle if nothing restores it. Real time is
// (re)established via the Settings screen "Set Time" field or a BLE
// set_time command.

// Tries the hardware RTC first, then falls back to the last-known time
// saved in NVS. Call once after NVS init (and after i2c_init(), which the
// hardware RTC path depends on). Returns false only if neither source had
// anything (clock starts at the epoch).
bool soft_rtc_init(void);

// Sets the system clock (settimeofday), writes it to the hardware RTC if
// one was found, and persists it to NVS as the fallback source too. Call
// this from the Settings screen and from the BLE set_time command handler.
void soft_rtc_set_time(time_t t);

// ── Timezone offset ──────────────────────────────────────────────────────
// The system clock, hardware RTC, NVS fallback, and every stored/synced
// timestamp (session/scan created_at, BLE set_time) are all true UTC — that
// consistency matters, since the backend treats every timestamp it receives
// as a Unix (UTC) epoch value. The offset below is purely a display/entry
// concern: it's how "what the user sees on screen" and "what the user types
// into the Date & Time screen" get translated to/from that underlying UTC
// clock, without the stored data itself ever becoming timezone-dependent.
// Persisted via AppSettings.tz_offset_min (nvs_storage.h), minutes east of
// UTC, whole-hour steps, defaults to 0 (UTC) — set via the Date & Time screen.

int16_t soft_rtc_get_tz_offset_min(void);
void    soft_rtc_set_tz_offset_min(int16_t minutes);

// Current moment as local calendar fields (system UTC clock + the
// configured offset) — use this instead of localtime(time(NULL)) anywhere
// the device displays "now" to the user (clock headers, default session
// names, etc).
void soft_rtc_get_local_tm(struct tm *out);

// Takes calendar fields the user entered as intended LOCAL wall-clock time,
// converts to true UTC using the configured offset, and applies via
// soft_rtc_set_time() exactly as before. Use this (not mktime()+
// soft_rtc_set_time() directly) from the Date & Time screen's "Set Time".
void soft_rtc_set_local_tm(const struct tm *local_tm);

#endif
