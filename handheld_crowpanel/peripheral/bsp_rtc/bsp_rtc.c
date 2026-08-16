#include "bsp_rtc.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include <sys/time.h>
#include <time.h>

static const char *TAG = "bsp_rtc";

// ---------------------------------------------------------------------------
// DS3231 — I2C address 0x68 (fixed), register map:
//   0x00  Seconds  (BCD 00-59)
//   0x01  Minutes  (BCD 00-59)
//   0x02  Hours    (BCD 00-23, 24-hour mode: bit 6 = 0)
//   0x03  Day-of-week (1-7, not used for timekeeping here)
//   0x04  Date     (BCD 01-31)
//   0x05  Month    (BCD 01-12, bit 7 = century - ignore)
//   0x06  Year     (BCD 00-99, relative to 2000)
// ---------------------------------------------------------------------------
#define DS3231_ADDR     0x68
#define DS3231_REG_TIME 0x00 // sequential read/write starts here

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_ready = false;

static inline uint8_t bcd2dec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
static inline uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

bool rtc_init(void) {
    s_dev = i2c_dev_register(DS3231_ADDR);
    if (!s_dev) {
        ESP_LOGW(TAG, "i2c_dev_register(0x%02X) failed", DS3231_ADDR);
        return false;
    }

    uint8_t buf[7];
    esp_err_t err = i2c_read_reg(s_dev, DS3231_REG_TIME, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 not detected: %s", esp_err_to_name(err));
        return false;
    }

    struct tm tm = {0};
    tm.tm_sec  = bcd2dec(buf[0] & 0x7F);
    tm.tm_min  = bcd2dec(buf[1] & 0x7F);
    tm.tm_hour = bcd2dec(buf[2] & 0x3F); // mask bits 7:6 (12/24 and AM/PM flags)
    // buf[3] = day-of-week - not needed
    tm.tm_mday = bcd2dec(buf[4] & 0x3F);
    tm.tm_mon  = bcd2dec(buf[5] & 0x1F) - 1; // DS3231: 1-12 -> struct tm: 0-11
    tm.tm_year = bcd2dec(buf[6]) + 100;      // DS3231: 0-99 (offset from 2000)
                                              // struct tm: offset from 1900 -> add 100

    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    s_ready = true;
    ESP_LOGI(TAG, "RTC synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

bool rtc_is_ready(void) {
    return s_ready;
}

bool rtc_set_time(time_t t) {
    if (!s_dev) return false;

    struct tm *tm = gmtime(&t); // DS3231 stores UTC

    // 8-byte write: register address + 7 time bytes
    uint8_t buf[8];
    buf[0] = DS3231_REG_TIME;
    buf[1] = dec2bcd((uint8_t)tm->tm_sec);
    buf[2] = dec2bcd((uint8_t)tm->tm_min);
    buf[3] = dec2bcd((uint8_t)tm->tm_hour);      // 24-hour mode (bit 6 = 0 automatically)
    buf[4] = (uint8_t)(tm->tm_wday + 1);         // day-of-week 1-7 (not BCD, not used)
    buf[5] = dec2bcd((uint8_t)tm->tm_mday);
    buf[6] = dec2bcd((uint8_t)(tm->tm_mon + 1)); // struct tm 0-11 -> DS3231 1-12
    buf[7] = dec2bcd((uint8_t)(tm->tm_year % 100)); // years since 1900 -> last 2 digits of year

    esp_err_t err = i2c_write(s_dev, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 write failed: %s", esp_err_to_name(err));
        return false;
    }

    s_ready = true; // a successful write also confirms the chip is present
    ESP_LOGI(TAG, "RTC updated: %04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return true;
}
