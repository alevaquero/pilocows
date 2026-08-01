#ifndef _SOFT_RTC_H_
#define _SOFT_RTC_H_

#include <time.h>
#include <stdbool.h>

// CrowPanel has no DS3231/RTC chip (unlike the SC01 handheld). Time-keeping is
// software-only: the system clock resets to the epoch on every power cycle
// unless restored here from the last known value persisted in NVS. Real time
// is (re)established via the Settings screen "Set Time" field or a BLE
// set_time command once BLE is wired up.

// Restores the system clock from the last-known time saved in NVS (best
// effort — will still be stale/wrong after any period without power, since
// there is no battery-backed hardware clock). Call once after NVS init.
// Returns false if no saved time was found (clock starts at the epoch).
bool soft_rtc_init(void);

// Sets the system clock (settimeofday) and persists it to NVS so the next
// boot can restore an approximate time. Call this from the Settings screen
// and from the BLE set_time command handler.
void soft_rtc_set_time(time_t t);

#endif
