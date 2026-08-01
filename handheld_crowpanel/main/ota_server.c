#include "ota_server.h"
#include "ui_manager.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

// ── Embedded web UI — single HTML page, no external assets. ────────────────
// JavaScript POSTs the raw .bin file as application/octet-stream so the
// server handler doesn't need to parse multipart boundaries.
static const char s_html[] =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Pilocows OTA</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:440px;margin:40px auto;padding:0 16px;text-align:center}"
    "h2{margin-bottom:24px}"
    "input[type=file]{display:block;margin:0 auto 16px;font-size:15px}"
    "button{padding:10px 28px;font-size:15px;cursor:pointer;border-radius:6px}"
    "#s{margin-top:20px;font-weight:bold;min-height:22px}"
    "</style>"
    "</head>"
    "<body>"
    "<h2>Pilocows Firmware Update</h2>"
    "<input type='file' id='f' accept='.bin'>"
    "<button onclick='go()'>Flash Firmware</button>"
    "<div id='s'></div>"
    "<script>"
    "function go(){"
    "var f=document.getElementById('f').files[0];"
    "if(!f){alert('Select a .bin file');return;}"
    "var x=new XMLHttpRequest();"
    "x.open('POST','/update');"
    "x.setRequestHeader('Content-Type','application/octet-stream');"
    "x.onload=function(){document.getElementById('s').innerText="
    "x.status===200?'Done! Rebooting...':'Error: '+x.responseText;};"
    "x.upload.onprogress=function(e){"
    "if(e.lengthComputable)"
    "document.getElementById('s').innerText='Uploading '+Math.round(e.loaded/e.total*100)+'%';};"
    "x.onerror=function(){document.getElementById('s').innerText='Network error';};"
    "x.send(f);"
    "}"
    "</script>"
    "</body>"
    "</html>";

// Reboot after the HTTP response is flushed
static void restart_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

// GET / — serve the upload page
static esp_err_t handle_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_html, (ssize_t)sizeof(s_html) - 1);
    return ESP_OK;
}

// POST /update — stream raw firmware binary into the next OTA partition
static esp_err_t handle_update(httpd_req_t *req) {
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Writing to: %s (offset 0x%08lx)", update_part->label,
             (unsigned long)update_part->address);

    lvgl_port_lock(0);
    ui_manager_show_status("OTA: updating...");
    lvgl_port_unlock();

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return err;
    }

    // Receive firmware in 4 KB chunks — static buffer to avoid stack overflow
    static char rx_buf[4096];
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_recv = remaining < (int)sizeof(rx_buf) ? remaining : (int)sizeof(rx_buf);
        int received = httpd_req_recv(req, rx_buf, to_recv);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Receive error");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota_handle, rx_buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
            return err;
        }
        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Verify failed");
        return err;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot failed");
        return err;
    }

    ESP_LOGI(TAG, "OTA complete - rebooting");
    httpd_resp_sendstr(req, "OK");

    xTaskCreate(restart_task, "ota_restart", 1024, NULL, 5, NULL);
    return ESP_OK;
}

void ota_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // extra stack for OTA write loop
    config.recv_wait_timeout = 30; // seconds — allow slow connections

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = handle_root, .user_ctx = NULL };
    httpd_uri_t update = { .uri = "/update", .method = HTTP_POST, .handler = handle_update, .user_ctx = NULL };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &update);

    ESP_LOGI(TAG, "OTA server ready - http://<ip>/");
}
