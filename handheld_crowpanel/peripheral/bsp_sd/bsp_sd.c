#include "bsp_sd.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

static const char *TAG = "bsp_sd";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t sd_init(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = 10000;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = SD_GPIO_CLK;
    slot_config.cmd = SD_GPIO_CMD;
    slot_config.d0 = SD_GPIO_D0;
    // NOTE: SDMMC_SLOT_CONFIG_DEFAULT() pre-fills d1/d2/d3 with this board's
    // default pins (40/41/42), and GPIO42 happens to be the GT911 touch
    // controller's INT line. These can't be overridden to "unused" on
    // ESP32-P4 slot 0 — its d0-d3 are dedicated (non-GPIO-matrix) pins, and
    // the driver rejects GPIO_NUM_NC there with ESP_ERR_INVALID_ARG ("doesn't
    // support routing from GPIO matrix"). So a failed mount's cleanup path
    // WILL reconfigure GPIO42 away from the touch controller's interrupt
    // setup — callers must restore it (see touch_init()) before touching
    // LVGL after a failed sd_init().
    slot_config.width = 1; // 1-line SDMMC
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SD filesystem (card unformatted or absent)");
        } else {
            ESP_LOGE(TAG, "Failed to init SD card: %s", esp_err_to_name(err));
        }
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

bool sd_is_mounted(void) {
    return s_mounted;
}
