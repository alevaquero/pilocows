#include "rtc.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include <sys/time.h>
#include <time.h>

static const char *TAG = "rtc";

// ---------------------------------------------------------------------------
// DS3231 — I2C address 0x68 (fixed), register map:
//   0x00  Seconds  (BCD 00–59)
//   0x01  Minutes  (BCD 00–59)
//   0x02  Hours    (BCD 00–23, 24-hour mode: bit 6 = 0)
//   0x03  Day-of-week (1–7, not used for timekeeping here)
//   0x04  Date     (BCD 01–31)
//   0x05  Month    (BCD 01–12, bit 7 = century — ignore)
//   0x06  Year     (BCD 00–99, relative to 2000)
// ---------------------------------------------------------------------------
#define DS3231_ADDR     0x68
#define DS3231_REG_TIME 0x00   // sequential read/write starts here

#define I2C_TIMEOUT_MS  100

static inline uint8_t bcd2dec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
static inline uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool rtc_init(void)
{
    // Install I2C driver on the dedicated RTC bus (I2C_NUM_1, GPIO 13/14).
    // This is independent from the touch I2C (I2C_NUM_0) installed by display_init().
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = RTC_SDA_PIN,
        .scl_io_num       = RTC_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed    = RTC_I2C_FREQ_HZ,
        },
    };
    esp_err_t err = i2c_param_config(RTC_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return false;
    }
    err = i2c_driver_install(RTC_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return false;
    }

    // Read 7 bytes starting at register 0x00
    uint8_t reg = DS3231_REG_TIME;
    uint8_t buf[7];
    err = i2c_master_write_read_device(
        RTC_I2C_PORT, DS3231_ADDR,
        &reg, 1,
        buf, sizeof(buf),
        pdMS_TO_TICKS(I2C_TIMEOUT_MS)
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 not detected: %s", esp_err_to_name(err));
        return false;
    }

    struct tm tm = {};
    tm.tm_sec  = bcd2dec(buf[0] & 0x7F);
    tm.tm_min  = bcd2dec(buf[1] & 0x7F);
    tm.tm_hour = bcd2dec(buf[2] & 0x3F);  // mask bits 7:6 (12/24 and AM/PM flags)
    // buf[3] = day-of-week — not needed
    tm.tm_mday = bcd2dec(buf[4] & 0x3F);
    tm.tm_mon  = bcd2dec(buf[5] & 0x1F) - 1;  // DS3231: 1-12 → struct tm: 0-11
    tm.tm_year = bcd2dec(buf[6]) + 100;         // DS3231: 0–99 (offset from 2000)
                                                 // struct tm: offset from 1900 → add 100

    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "RTC synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

bool rtc_set_time(time_t t)
{
    struct tm *tm = gmtime(&t);  // DS3231 stores UTC

    // 8-byte write: register address + 7 time bytes
    uint8_t buf[8];
    buf[0] = DS3231_REG_TIME;
    buf[1] = dec2bcd((uint8_t)tm->tm_sec);
    buf[2] = dec2bcd((uint8_t)tm->tm_min);
    buf[3] = dec2bcd((uint8_t)tm->tm_hour);    // 24-hour mode (bit 6 = 0 automatically)
    buf[4] = (uint8_t)(tm->tm_wday + 1);       // day-of-week 1–7 (not BCD, not used)
    buf[5] = dec2bcd((uint8_t)tm->tm_mday);
    buf[6] = dec2bcd((uint8_t)(tm->tm_mon + 1)); // struct tm 0-11 → DS3231 1-12
    buf[7] = dec2bcd((uint8_t)(tm->tm_year % 100)); // years since 1900 → last 2 digits of year

    esp_err_t err = i2c_master_write_to_device(
        RTC_I2C_PORT, DS3231_ADDR,
        buf, sizeof(buf),
        pdMS_TO_TICKS(I2C_TIMEOUT_MS)
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 write failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "RTC updated: %04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return true;
}
