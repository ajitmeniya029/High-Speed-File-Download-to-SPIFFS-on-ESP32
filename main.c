#include <stdio.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "spiffs.h"

#include "wifi.h"
#include "https_client.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    esp_err_t ret;

    // Init NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS initialized");

    // Init SPIFFS
    if (spiffs_init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize SPIFFS");
        return;
    }
    ESP_LOGI(TAG, "✅ SPIFFS mounted successfully");

    // Init Wi-Fi
    wifi_init_sta();
    ESP_LOGI(TAG, "✅ Wi-Fi initialization complete");

    // Let Wi-Fi connect
    vTaskDelay(pdMS_TO_TICKS(5000));

    // File download
    const char *url = "https://jumpshare.com/s/qjrb7NvwsWr9DjREgHYK";
    const char *filepath = "/spiffs/sample.txt";

    ret = https_download_file(url, filepath);
    if (ret == ESP_OK) {
        struct stat st;
        if (stat(filepath, &st) == 0) {
            ESP_LOGI(TAG, "📂 File downloaded successfully to %s (%ld bytes)",
                     filepath, st.st_size);
        } else {
            ESP_LOGE(TAG, "❌ Downloaded file not found on SPIFFS!");
        }
    } else {
        ESP_LOGE(TAG, "❌ File download failed");
    }

    // Keep app alive
    while (1) {
        ESP_LOGI(TAG, "App running...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
