#pragma once
#include <time.h>
#include <stdbool.h>

// DS3231 RTC — I2C address 0x68, shares bus with touch (I2C_NUM_0, SDA=6, SCL=5).
// Must be called AFTER display_init(), which installs the I2C driver.

// Reads the DS3231 and syncs the ESP32 system clock.
// Returns false if the DS3231 is not found on the I2C bus.
bool rtc_init(void);

// Writes a time_t value (UTC) to the DS3231.
// Call this alongside settimeofday() whenever the user or BLE sets the time.
bool rtc_set_time(time_t t);
