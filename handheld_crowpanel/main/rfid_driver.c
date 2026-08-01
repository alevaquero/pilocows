#include "rfid_driver.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "rfid";

#define RFID_UART_NUM     UART_NUM_1
#define RFID_RX_GPIO      26
#define RFID_BAUD         9600
#define RFID_TASK_STACK   4096
#define RFID_TASK_PRIO    5

// ---------------------------------------------------------------------------
// WL-134 packet format (30 bytes total, 9600 baud, 8N1)
//
//  [0]     0x02  STX
//  [1..10] 10 ASCII hex chars — animal ID, LSB-first (5 bytes / 40 bits raw)
//  [11..14] 4 ASCII hex chars — country code, LSB-first (2 bytes / 10 bits)
//  [15]    ASCII '0'/'1' — data flag
//  [16]    ASCII '0'/'1' — animal flag
//  [17..20] 4 ASCII hex chars — reserved0
//  [21..26] 6 ASCII hex chars — reserved1
//  [27]    XOR checksum of bytes [1..26]
//  [28]    ~checksum (one's complement of byte 27)
//  [29]    0x03  ETX
//
// Final tag string: sprintf("%03d%012lld", country, animal_id)
// Example: country=900, id=158002044774 -> "900158002044774"
// ---------------------------------------------------------------------------

#define RFID_PACKET_SIZE      30
#define RFID_PACKET_STX       0x02
#define RFID_PACKET_ETX       0x03

#define RFID_OFF_ID           1
#define RFID_OFF_COUNTRY      11
#define RFID_OFF_DATA_FLAG    15
#define RFID_OFF_ANIMAL_FLAG  16
#define RFID_OFF_CHECKSUM     27
#define RFID_OFF_CHK_INV      28
#define RFID_OFF_ETX          29

#define RFID_BUF_SIZE         128

static rfid_callback_t s_callback = NULL;
static TaskHandle_t s_rfid_task = NULL;
static uint32_t s_scan_count = 0;

// Decode an ASCII hex string stored LSB-first into a uint64.
static uint64_t hex_lsb_ascii_to_uint64(const uint8_t *text, uint8_t len)
{
    uint64_t value = 0;
    uint8_t i = len;
    do {
        i--;
        uint8_t nibble = text[i];
        nibble = (nibble >= 'A') ? (nibble - 'A' + 10) : (nibble - '0');
        value = (value << 4) + nibble;
    } while (i != 0);
    return value;
}

// Parse one complete 30-byte WL-134 packet into an EID string.
static bool parse_packet(const uint8_t *pkt, RfidScan *scan_out)
{
    if (pkt[0] != RFID_PACKET_STX || pkt[RFID_OFF_ETX] != RFID_PACKET_ETX) {
        ESP_LOGW(TAG, "Bad framing: STX=%02X ETX=%02X", pkt[0], pkt[RFID_OFF_ETX]);
        return false;
    }

    uint8_t checksum = 0;
    for (int i = RFID_OFF_ID; i < RFID_OFF_CHECKSUM; i++) {
        checksum ^= pkt[i];
    }
    if (checksum != pkt[RFID_OFF_CHECKSUM]) {
        ESP_LOGW(TAG, "Checksum mismatch: calc=%02X pkt=%02X", checksum, pkt[RFID_OFF_CHECKSUM]);
        return false;
    }
    if ((uint8_t)(~checksum) != pkt[RFID_OFF_CHK_INV]) {
        ESP_LOGW(TAG, "Inverted checksum mismatch: ~calc=%02X pkt=%02X",
                 (uint8_t)(~checksum), pkt[RFID_OFF_CHK_INV]);
        return false;
    }

    uint64_t animal_id = hex_lsb_ascii_to_uint64(&pkt[RFID_OFF_ID], 10);
    uint64_t country   = hex_lsb_ascii_to_uint64(&pkt[RFID_OFF_COUNTRY], 4);

    snprintf(scan_out->eid, sizeof(scan_out->eid), "%03llu%012llu",
             (unsigned long long)country, (unsigned long long)animal_id);
    scan_out->timestamp = (uint32_t)time(NULL);

    return true;
}

// RFID reader task — runs continuously, decodes packets as they arrive.
static void rfid_read_task(void *arg) {
    uint8_t buf[RFID_BUF_SIZE];
    int stored = 0;

    ESP_LOGI(TAG, "RFID read task started");

    while (1) {
        int n = uart_read_bytes(RFID_UART_NUM, buf + stored,
                                 sizeof(buf) - stored - 1,
                                 pdMS_TO_TICKS(200));
        if (n <= 0) continue;

        stored += n;

        int consumed = 0;
        while (stored - consumed >= RFID_PACKET_SIZE) {
            if (buf[consumed] != RFID_PACKET_STX) {
                ESP_LOGW(TAG, "Skipping non-STX byte: %02X", buf[consumed]);
                consumed++;
                continue;
            }

            const uint8_t *pkt = &buf[consumed];
            RfidScan scan;
            if (parse_packet(pkt, &scan)) {
                s_scan_count++;
                ESP_LOGI(TAG, "Tag scanned: %s (scan #%lu)", scan.eid, (unsigned long)s_scan_count);
                if (s_callback) {
                    s_callback(&scan);
                }
            }
            consumed += RFID_PACKET_SIZE;
        }

        if (consumed > 0) {
            stored -= consumed;
            if (stored > 0) memmove(buf, buf + consumed, stored);
        }
    }
}

esp_err_t rfid_init(rfid_callback_t callback) {
    if (!callback) return ESP_ERR_INVALID_ARG;

    s_callback = callback;
    s_scan_count = 0;

    uart_config_t uart_cfg = {
        .baud_rate = RFID_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t err = uart_param_config(RFID_UART_NUM, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(RFID_UART_NUM, UART_PIN_NO_CHANGE, RFID_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(RFID_UART_NUM, RFID_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreate(rfid_read_task, "rfid", RFID_TASK_STACK, NULL, RFID_TASK_PRIO, &s_rfid_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RFID task");
        uart_driver_delete(RFID_UART_NUM);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "RFID reader ready - UART%d RX=GPIO%d @ %d baud", RFID_UART_NUM, RFID_RX_GPIO, RFID_BAUD);
    return ESP_OK;
}

esp_err_t rfid_deinit(void) {
    if (s_rfid_task) {
        vTaskDelete(s_rfid_task);
        s_rfid_task = NULL;
    }
    uart_driver_delete(RFID_UART_NUM);
    s_callback = NULL;
    ESP_LOGI(TAG, "RFID driver deinitialized");
    return ESP_OK;
}

uint32_t rfid_get_scan_count(void) {
    return s_scan_count;
}

void rfid_clear_scans(void) {
    s_scan_count = 0;
    ESP_LOGI(TAG, "Scan counter cleared");
}
