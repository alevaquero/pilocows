#include "bsp_mic.h"
#include "esp_log.h"

static const char *TAG = "bsp_mic";

static i2s_chan_handle_t s_rx_chan = NULL;
static bool s_ready = false;
// Kept so mic_set_gain() can pass the full slot config back to
// i2s_channel_reconfig_pdm_rx_slot() (it takes the whole struct, not just
// the one field being changed).
static i2s_pdm_rx_slot_config_t s_slot_cfg;

esp_err_t mic_init(void) {
    if (s_ready) return ESP_OK;

    i2s_chan_config_t rx_chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 256,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = true,
        .allow_pd = false,
        .intr_priority = 0,
    };
    esp_err_t err = i2s_new_channel(&rx_chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) return err;

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = {
            .sample_rate_hz = MIC_SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .dn_sample_mode = I2S_PDM_DSR_8S,
            .bclk_div = 8,
        },
        // PDM mode's data bit-width is fixed at 16.
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_PDM_SLOT_LEFT,
            .hp_en = true,
            .hp_cut_off_freq_hz = 35.5,
            // Hardware PDM->PCM digital gain (1-15, factory default is 1 -
            // effectively unamplified). Raised for better pickup at a
            // distance; the recording screen also normalizes quiet clips
            // after capture, so this doesn't need to be pushed to the max
            // (which would mostly just amplify the mic's self-noise floor).
            // User-adjustable at runtime via mic_set_gain() (see Settings).
            .amplify_num = MIC_GAIN_DEFAULT,
        },
        .gpio_cfg = {
            .clk = MIC_GPIO_CLK,
            .din = MIC_GPIO_DIN,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    err = i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_rx_cfg);
    if (err != ESP_OK) return err;

    err = i2s_channel_enable(s_rx_chan);
    if (err != ESP_OK) return err;

    s_slot_cfg = pdm_rx_cfg.slot_cfg;
    s_ready = true;
    ESP_LOGI(TAG, "PDM mic RX channel ready (%d Hz)", MIC_SAMPLE_RATE_HZ);
    return ESP_OK;
}

bool mic_is_ready(void) {
    return s_ready;
}

esp_err_t mic_read(int16_t *buf, size_t max_samples, size_t *samples_read, TickType_t timeout) {
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_chan, buf, max_samples * sizeof(int16_t), &bytes_read, timeout);
    if (samples_read) *samples_read = bytes_read / sizeof(int16_t);
    return err;
}

esp_err_t mic_set_gain(uint8_t amplify_num) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (amplify_num < MIC_GAIN_MIN) amplify_num = MIC_GAIN_MIN;
    if (amplify_num > MIC_GAIN_MAX) amplify_num = MIC_GAIN_MAX;
    if (amplify_num == s_slot_cfg.amplify_num) return ESP_OK;

    i2s_pdm_rx_slot_config_t new_cfg = s_slot_cfg;
    new_cfg.amplify_num = amplify_num;

    // i2s_channel_reconfig_pdm_rx_slot() requires the channel to be
    // disabled first if it has already been started.
    esp_err_t err = i2s_channel_disable(s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_reconfig_pdm_rx_slot(s_rx_chan, &new_cfg);
    if (err == ESP_OK) {
        s_slot_cfg = new_cfg;
    } else {
        ESP_LOGE(TAG, "i2s_channel_reconfig_pdm_rx_slot failed: %s", esp_err_to_name(err));
    }

    esp_err_t enable_err = i2s_channel_enable(s_rx_chan);
    if (enable_err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(enable_err));
        return enable_err;
    }
    return err;
}

uint8_t mic_get_gain(void) {
    return s_slot_cfg.amplify_num;
}
