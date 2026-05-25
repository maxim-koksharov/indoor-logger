#include "Display.h"
#include "ens160.h"
#include "aht21.h"
#include "button.h"
#include "data_storage.h"
#include "wifi_sync.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"

static const char *TAG = "client";

#define SDA_PIN 4
#define SCL_PIN 5

#define SENSOR_READ_INTERVAL_MS 5000
#define WIFI_SYNC_INTERVAL_MS 30000
#define MAIN_LOOP_DELAY_MS 500

#ifndef CONFIG_CLIENT_ID
#define CONFIG_CLIENT_ID "test_client"
#endif

#ifndef CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC
#define CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC 10
#endif

static Display display;
static const font_info_t *display_font = NULL;

static aht21_data_t last_aht_data = {0};
static ens160_data_t last_ens_data = {0};

static uint32_t client_uptime_sec = 0;

static void client_task(void *pvParameters) {
    TickType_t last_sensor_read = 0;
    TickType_t last_wifi_sync = 0;
    int loop_count = 0;
    bool wifi_connected = false;

    ESP_LOGI(TAG, "[TASK] Client task started (client_id=%s, display=%ds)",
             CONFIG_CLIENT_ID, CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC);

    while (1) {
        TickType_t now = xTaskGetTickCount();
        loop_count++;
        esp_task_wdt_reset();

        if ((now - last_sensor_read) >= pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS)) {
            ESP_LOGI(TAG, "[SENSORS] Reading...");

            aht21_data_t aht_data = {0};
            ens160_data_t ens_data = {0};

            int aht_ret = aht21_read(I2C_NUM_0, &aht_data);
            if (aht_ret == 0) {
                last_aht_data = aht_data;
                int t_int = (int)aht_data.temperature;
                int t_dec = (int)(aht_data.temperature * 10) % 10;
                if (t_dec < 0) t_dec = -t_dec;
                ESP_LOGI(TAG, "[SENSORS] AHT21: T=%d.%d H=%d", t_int, t_dec, (int)aht_data.humidity);
            } else {
                ESP_LOGW(TAG, "[SENSORS] AHT21 failed: %d", aht_ret);
            }

#if ENS160_ENABLE
            int ens_ret = ens160_read(I2C_NUM_0, &ens_data);
            if (ens_ret == 0) {
                last_ens_data = ens_data;
                ESP_LOGI(TAG, "[SENSORS] ENS160: eCO2=%u TVOC=%u AQI=%u", ens_data.eco2, ens_data.tvoc, ens_data.aqi);
            } else {
                ESP_LOGW(TAG, "[SENSORS] ENS160 failed: %d", ens_ret);
            }

            ens160_set_env(I2C_NUM_0, last_aht_data.temperature, last_aht_data.humidity);
#else
            (void)ens_data;
            ESP_LOGI(TAG, "[SENSORS] ENS160 disabled");
#endif

            client_uptime_sec = (uint32_t)(now * portTICK_PERIOD_MS / 1000);

            // Save record
            client_record_t record = {
                .timestamp = client_uptime_sec,
                .uptime_sec = client_uptime_sec,
                .temperature = last_aht_data.temperature,
                .humidity = last_aht_data.humidity,
                .eco2 = last_ens_data.eco2,
                .tvoc = last_ens_data.tvoc,
                .aqi = last_ens_data.aqi,
                .synced = false
            };

            if (data_storage_append(&record) == 0) {
                ESP_LOGI(TAG, "[STORAGE] Saved, total=%d", data_storage_get_count());
            } else {
                ESP_LOGW(TAG, "[STORAGE] Append failed, SPIFFS may be full");
            }

            // Update display with latest readings
            char line1[24], line2[24];
            int t_int = (int)last_aht_data.temperature;
            int t_dec = (int)(last_aht_data.temperature * 10) % 10;
            if (t_dec < 0) t_dec = -t_dec;
            if (last_ens_data.aqi > 0) {
                snprintf(line1, sizeof(line1), "%d.%dC %d%% AQI%u", t_int, t_dec, (int)last_aht_data.humidity, last_ens_data.aqi);
                snprintf(line2, sizeof(line2), "eCO2%u TVOC%u", last_ens_data.eco2, last_ens_data.tvoc);
            } else {
                snprintf(line1, sizeof(line1), "%d.%dC %d%%", t_int, t_dec, (int)last_aht_data.humidity);
                snprintf(line2, sizeof(line2), "ENS warming...");
            }
            display_clear_fb(&display);
            display_draw_string_scaled(&display, display_font, 0, 0, line1, 2);
            display_draw_string_scaled(&display, display_font, 0, 17, line2, 2);
            display_present(&display);

            last_sensor_read = now;
        }

        // WiFi sync every 30 seconds
        if ((now - last_wifi_sync) >= pdMS_TO_TICKS(WIFI_SYNC_INTERVAL_MS)) {
            if (!wifi_connected) {
                ESP_LOGI(TAG, "[WIFI] Connecting...");
                if (wifi_sync_connect_to_server("AirMon-Server", "12345678") == ESP_OK) {
                    wifi_connected = true;
                    ESP_LOGI(TAG, "[WIFI] Connected!");
                }
            }

            if (wifi_connected) {
                ESP_LOGI(TAG, "[WIFI] Uploading...");
                if (wifi_sync_upload_unsynced() == ESP_OK) {
                    ESP_LOGI(TAG, "[WIFI] Upload OK");
                } else {
                    wifi_connected = false;
                }
            }

            last_wifi_sync = now;
        }

        if (loop_count % 30 == 0) {
            ESP_LOGI(TAG, "[LOOP] Heartbeat: %d, heap=%uK", loop_count, (unsigned int)(esp_get_free_heap_size() / 1024));
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Client starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    display_init(&display, SDA_PIN, SCL_PIN, SSD1306_I2C_ADDR_0);

#if ENS160_ENABLE
    int ens_init_ret = ens160_init(I2C_NUM_0);
    if (ens_init_ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize ENS160");
    }
#else
    int ens_init_ret = -1;
#endif
    int aht_init_ret = aht21_init(I2C_NUM_0);
    if (aht_init_ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize AHT21");
    }

    button_init();

    if (data_storage_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize data storage");
    }
    ESP_LOGI(TAG, "Data storage initialized, %d records", data_storage_get_count());

    if (wifi_sync_init(CONFIG_CLIENT_ID) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi sync");
    }

    display_font = font_builtin_fonts[FONT_FACE_GLCD5x7];

    display_clear_fb(&display);
    display_draw_string_scaled(&display, display_font, 0, 0, "Client", 2);
    display_draw_string_scaled(&display, display_font, 0, 17, "Ready", 2);
    display_present(&display);
    display_on(&display);

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Self-test summary
    {
        size_t spiffs_total = 0, spiffs_used = 0;
        esp_err_t spiffs_ret = esp_spiffs_info("storage", &spiffs_total, &spiffs_used);
        int storage_count = data_storage_get_count();
        ESP_LOGI(TAG, "=== SELF-TEST ===");
        ESP_LOGI(TAG, "  NVS: OK");
        ESP_LOGI(TAG, "  I2C: init OK (SDA=GPIO4 SCL=GPIO5)");
        ESP_LOGI(TAG, "  Display: initialized");
#if ENS160_ENABLE
        ESP_LOGI(TAG, "  ENS160: %s", (ens_init_ret == 0) ? "detected" : "not detected or warming");
#else
        ESP_LOGI(TAG, "  ENS160: disabled (ENS160_ENABLE=0)");
#endif
        ESP_LOGI(TAG, "  AHT21: %s", (aht_init_ret == 0) ? "detected" : "failed");
        ESP_LOGI(TAG, "  SPIFFS: %s (total=%u used=%u)",
                 spiffs_ret == ESP_OK ? "OK" : "FAIL", spiffs_total, spiffs_used);
        ESP_LOGI(TAG, "  Storage: %d records saved", storage_count);
        ESP_LOGI(TAG, "  Client ID: %s", CONFIG_CLIENT_ID);
        ESP_LOGI(TAG, "  Heap at init: %d KB", (int)(esp_get_free_heap_size() / 1024));
        ESP_LOGI(TAG, "=== END SELF-TEST ===");
    }

    // Create client task with 4KB stack
    xTaskCreate(client_task, "client_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Client task created");
    
    // Main loop just idles
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
