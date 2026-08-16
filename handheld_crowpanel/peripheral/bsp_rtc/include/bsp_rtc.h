#ifndef _BSP_RTC_H_
#define _BSP_RTC_H_

#include <time.h>
#include <stdbool.h>

// DS3231 real-time clock — I2C address 0x68 (fixed). Shares the board's
// existing I2C bus (bsp_i2c, GPIO45 SDA / GPIO46 SCL) with the GT911 touch
// controller and STC8H1KXX management MCU — no dedicated pins needed, wire
// the module to the board's external I2C header. Ported from the SC01
// Plus handheld's driver (src/rtc/rtc.cpp), adapted from that board's own
// dedicated I2C bus to this board's shared one.
//
// Call rtc_init() after i2c_init() (bsp_i2c) has installed the shared bus.

// Reads the DS3231 and syncs the ESP32 system clock (settimeofday) from it.
// Returns false if the DS3231 doesn't answer on the bus — non-fatal;
// callers should fall back to another time source (e.g. soft_rtc's NVS-
// persisted last-known time) rather than treating this as fatal.
bool rtc_init(void);

// True once rtc_init() has found and synced from the DS3231.
bool rtc_is_ready(void);

// Writes a time_t value (UTC) to the DS3231. Call this whenever the system
// time is set (user entry, BLE set_time) so the RTC — which keeps ticking
// on its own coin-cell backup power across reboots/power-off — stays
// correct too, not just the volatile system clock.
bool rtc_set_time(time_t t);

#endif
