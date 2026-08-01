#ifndef _BSP_SD_H_
#define _BSP_SD_H_

#include <stdbool.h>
#include "esp_err.h"

// SDMMC 1-bit slot pins (CrowPanel Advance P4 onboard microSD).
#define SD_GPIO_CLK 43
#define SD_GPIO_CMD 44
#define SD_GPIO_D0  39

#define SD_MOUNT_POINT "/sdcard"

// Mount the SD card's FAT filesystem at SD_MOUNT_POINT. Returns an error if
// no card is present or it fails to mount — callers should treat this as
// non-fatal (audio notes just won't be available) rather than halting boot.
esp_err_t sd_init(void);

// True once sd_init() has succeeded.
bool sd_is_mounted(void);

#endif
