#include <stdio.h>
#include <string.h>
#include <errno.h>              // ✅ For errno + strerror
#include <unistd.h>             // ✅ For unlink()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "esp_http_client.h"     // ✅ For esp_http_client_* types & funcs
#include "esp_crt_bundle.h"      // ✅ For esp_crt_bundle_attach
#include "esp_spiffs.h"          // ✅ For esp_spiffs_info

static const char *TAG = "https_client";

#define MAX_RETRIES          3
#define MIN_SPEED_BPS        (400 * 1024)   // 400 KBps
#define HTTP_TIMEOUT_MS      5000           // 5 sec read timeout
#define BACKOFF_BASE_MS      1000           // 1 sec base backoff
#define WRITE_BUFFER_SIZE    32768           // 🚀 8 KB RAM buffer

static FILE *file_handle = NULL;
static size_t total_bytes = 0;
static int64_t start_time = 0;
static bool storage_error = false;

// 🚀 RAM buffer for fewer SPIFFS writes
static uint8_t write_buffer[WRITE_BUFFER_SIZE];
static size_t buffer_offset = 0;

static void flush_write_buffer(void)
{
    if (file_handle && buffer_offset > 0 && !storage_error) {
        size_t written = fwrite(write_buffer, 1, buffer_offset, file_handle);
        if (written != buffer_offset) {
            ESP_LOGE(TAG, "❌ Storage write error (%d)", ferror(file_handle));
            storage_error = true;
        } else {
            total_bytes += written;
        }
        buffer_offset = 0; // reset
    }
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "Header: %s = %s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            if (evt->data && evt->data_len > 0 && file_handle && !storage_error) {
                // Check free space before buffering
                size_t total = 0, used = 0;
                if (esp_spiffs_info("spiffs", &total, &used) == ESP_OK) {
                    size_t free_space = total - used;
                    if (free_space < evt->data_len) {
                        ESP_LOGE(TAG, "❌ Out of SPIFFS space! Aborting...");
                        storage_error = true;
                        break;
                    }
                }

                // 🚀 Buffer the data
                size_t remaining = evt->data_len;
                const uint8_t *ptr = (const uint8_t *)evt->data;

                while (remaining > 0 && !storage_error) {
                    size_t space_left = WRITE_BUFFER_SIZE - buffer_offset;
                    size_t to_copy = (remaining < space_left) ? remaining : space_left;

                    memcpy(write_buffer + buffer_offset, ptr, to_copy);
                    buffer_offset += to_copy;
                    ptr += to_copy;
                    remaining -= to_copy;

                    // 🚀 Flush when buffer is full
                    if (buffer_offset == WRITE_BUFFER_SIZE) {
                        flush_write_buffer();
                    }
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            // 🚀 Flush any remaining data
            flush_write_buffer();
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGW(TAG, "HTTP_EVENT_REDIRECT to %s", (const char *)evt->data);
            break;
    }
    return ESP_OK;
}

esp_err_t https_download_file(const char *url, const char *filepath)
{
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ESP_LOGI(TAG, "🌍 Attempt %d to download %s", attempt, url);

        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = HTTP_TIMEOUT_MS,
            .buffer_size = 32768,   // 🚀 Larger RX buffer
            .buffer_size_tx = 8192 // 🚀 Larger TX buffer
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "❌ Failed to initialize HTTP client");
            return ESP_FAIL;
        }

        // ✅ Remove any existing file before writing
        unlink(filepath);

        file_handle = fopen(filepath, "wb");
        if (!file_handle) {
            ESP_LOGE(TAG, "❌ Failed to open file for writing: %s", filepath);
            ESP_LOGE(TAG, "   errno = %d (%s)", errno, strerror(errno));
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        total_bytes = 0;
        storage_error = false;
        buffer_offset = 0;
        start_time = esp_timer_get_time();

        ret = esp_http_client_perform(client);

        // 🚀 Flush any last buffered data
        flush_write_buffer();

        fclose(file_handle);
        file_handle = NULL;

        if (ret == ESP_OK && !storage_error) {
            int64_t end_time = esp_timer_get_time();
            double elapsed_sec = (end_time - start_time) / 1000000.0;
            double speed = (total_bytes / 1024.0) / elapsed_sec; // KBps

            ESP_LOGI(TAG, "📦 Downloaded %d bytes in %.2f sec (%.2f KB/s)",
                     total_bytes, elapsed_sec, speed);

            if (speed < (MIN_SPEED_BPS / 1024.0)) {
                ESP_LOGW(TAG, "⚠️ Download speed below 400 KBps requirement!");
            }

            ESP_LOGI(TAG, "✅ Download complete. Total bytes: %d", total_bytes);
            esp_http_client_cleanup(client);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "❌ Download failed (err=%s)", esp_err_to_name(ret));

            if (storage_error) {
                ESP_LOGE(TAG, "❌ Aborting due to storage error");
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }

            // Exponential backoff before retry
            int backoff_ms = BACKOFF_BASE_MS * (1 << (attempt - 1));
            ESP_LOGW(TAG, "⏳ Retrying in %d ms...", backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        }

        esp_http_client_cleanup(client);
    }

    return ESP_FAIL;
}
