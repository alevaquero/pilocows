#ifndef _NVS_STORAGE_H_
#define _NVS_STORAGE_H_

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    char language[3];
    bool buzzer_enabled;
    bool vibrator_enabled;
} AppSettings;

typedef struct {
    char eid[16];           // 11-digit EID + null terminator
    uint32_t timestamp;     // Unix timestamp
} ScanRecord;

typedef struct {
    ScanRecord scans[100];  // Max 100 scans in memory
    uint16_t count;
} ScanList;

// Settings
esp_err_t nvs_load_settings(AppSettings *settings);
esp_err_t nvs_save_settings(const AppSettings *settings);

// Scan list
esp_err_t nvs_load_scans(ScanList *scan_list);
esp_err_t nvs_save_scans(const ScanList *scan_list);
esp_err_t nvs_add_scan(const char *eid);
esp_err_t nvs_clear_scans(void);

#endif
