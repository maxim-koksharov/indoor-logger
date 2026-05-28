#include "Display.h"
#include "ens160.h"
#include "aht21.h"
#include "button.h"
#include "data_storage.h"
#include "wifi_sync.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "driver/i2c.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"
#include "esp_sleep.h"
#include "lwip/apps/sntp.h"

static const char *TAG = "client";

#define SDA_PIN 4
#define SCL_PIN 5

#define SENSOR_READ_INTERVAL_MS 300000
#define WIFI_SYNC_INTERVAL_MS 300000
#define DISPLAY_ON_MS 6000
#define DISPLAY_SCREEN_INTERVAL_MS 3000

#ifndef CONFIG_CLIENT_ID
#define CONFIG_CLIENT_ID "test_client"
#endif

#ifndef CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC
#define CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC 10
#endif

#ifndef CONFIG_CLIENT_WIFI_SSID
#define CONFIG_CLIENT_WIFI_SSID ""
#endif

#ifndef CONFIG_CLIENT_WIFI_PASS
#define CONFIG_CLIENT_WIFI_PASS ""
#endif

#ifndef CONFIG_CLIENT_DISCOVER_TIMEOUT_MS
#define CONFIG_CLIENT_DISCOVER_TIMEOUT_MS 3000
#endif

#ifndef CONFIG_CLIENT_BACKUP_SSID
#define CONFIG_CLIENT_BACKUP_SSID ""
#endif

#ifndef CONFIG_CLIENT_BACKUP_PASS
#define CONFIG_CLIENT_BACKUP_PASS ""
#endif

static Display display;
static const font_info_t *display_font = NULL;

static aht21_data_t last_aht_data = {0};
static ens160_data_t last_ens_data = {0};

static uint32_t client_uptime_sec = 0;
static int display_screen = 0;
static bool sntp_done = false;

static void init_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    while (timeinfo.tm_year < (2020 - 1900) && ++retry < 15) {
        ESP_LOGI(TAG, "Waiting for SNTP sync (%d/15)", retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    if (timeinfo.tm_year >= (2020 - 1900)) {
        ESP_LOGI(TAG, "SNTP synced");
        sntp_done = true;
    } else {
        ESP_LOGW(TAG, "SNTP sync failed, display will show --:--:--");
    }
}

static void update_display(void) {
    display_clear_fb(&display);
    int t_int = (int)last_aht_data.temperature;
    int t_dec = (int)(last_aht_data.temperature * 10) % 10;
    if (t_dec < 0) t_dec = -t_dec;

    if (display_screen == 0) {
        char line1[16], line2[16];
        snprintf(line1, sizeof(line1), "%d.%dC %d%%", t_int, t_dec, (int)last_aht_data.humidity);
        if (sntp_done) {
            time_t now = time(NULL);
            struct tm ti;
            localtime_r(&now, &ti);
            snprintf(line2, sizeof(line2), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        } else {
            snprintf(line2, sizeof(line2), "--:--:--");
        }
        int w1 = strlen(line1) * 12;
        int w2 = strlen(line2) * 12;
        display_draw_string_scaled(&display, display_font, (128 - w1) / 2, 0, line1, 2);
        display_draw_string_scaled(&display, display_font, (128 - w2) / 2, 17, line2, 2);
    } else {
        char line1[16], line2[16];
        snprintf(line1, sizeof(line1), "CO2 %u", last_ens_data.eco2);
        snprintf(line2, sizeof(line2), "VOC %u A%u", last_ens_data.tvoc, last_ens_data.aqi);
        display_draw_string_scaled(&display, display_font, 0, 0, line1, 2);
        display_draw_string_scaled(&display, display_font, 0, 17, line2, 2);
    }
    display_present(&display);
}

static void client_task(void *pvParameters) {
    TickType_t last_sensor_read = 0;
    TickType_t now_init = xTaskGetTickCount();
    last_sensor_read = now_init;
    
    bool wifi_initialized = false;
    bool display_active = false;
    TickType_t display_on_until = 0;
    TickType_t last_screen_switch = 0;
    int display_screen = 0;
    
    display_off(&display);
    
    ESP_LOGI(TAG, "[TASK] Client task started (client_id=%s)", CONFIG_CLIENT_ID);
    
    while (1) {
        TickType_t now = xTaskGetTickCount();
        esp_task_wdt_reset();
        
        bool button_pressed = button_is_pressed();
        
        if (display_active) {
            if (now >= display_on_until) {
                display_active = false;
                display_off(&display);
                ESP_LOGI(TAG, "[DISPLAY] Timeout, OFF");
            } else if ((now - last_screen_switch) >= pdMS_TO_TICKS(DISPLAY_SCREEN_INTERVAL_MS)) {
                display_screen = !display_screen;
                update_display();
                last_screen_switch = now;
            }
        }
        
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
            
            update_display();
            last_sensor_read = now;
            
            if (!wifi_initialized) {
                ESP_LOGI(TAG, "[WIFI] Connecting...");
                if (strlen(CONFIG_CLIENT_WIFI_SSID) > 0) {
                    if (wifi_sync_connect_to_server(CONFIG_CLIENT_WIFI_SSID, CONFIG_CLIENT_WIFI_PASS) == ESP_OK) {
                        wifi_initialized = true;
                        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                        ESP_LOGI(TAG, "[WIFI] Connected! Modem-sleep enabled");
                        if (!sntp_done) {
                            init_sntp();
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "[WIFI] No SSID configured (CONFIG_CLIENT_WIFI_SSID)");
                }
            }
            
            if (wifi_initialized) {
                ESP_LOGI(TAG, "[WIFI] Uploading...");
                if (wifi_sync_upload_unsynced() == ESP_OK) {
                    ESP_LOGI(TAG, "[WIFI] Upload OK");
                } else {
                    ESP_LOGW(TAG, "[WIFI] Upload failed");
                }
            }
        }
        
        if (button_pressed && !display_active) {
            display_active = true;
            display_on_until = now + pdMS_TO_TICKS(DISPLAY_ON_MS);
            display_screen = 0;
            last_screen_switch = now;
            display_on(&display);
            update_display();
            ESP_LOGI(TAG, "[BUTTON] Pressed, display ON for %dms", DISPLAY_ON_MS);
        }
        
        TickType_t time_to_sensor_ticks = pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS) - (now - last_sensor_read);
        TickType_t sleep_ticks = time_to_sensor_ticks;
        
        if (display_active) {
            TickType_t display_remaining = display_on_until - now;
            if (display_remaining < sleep_ticks) {
                sleep_ticks = display_remaining;
            }
        }
        
        if (button_pressed) {
            sleep_ticks = pdMS_TO_TICKS(100);
        }
        
        if (sleep_ticks > pdMS_TO_TICKS(10)) {
            if (wifi_initialized) {
                ESP_LOGI(TAG, "[WIFI] Stopping for light sleep");
                esp_err_t stop_ret = esp_wifi_stop();
                if (stop_ret != ESP_OK) {
                    ESP_LOGW(TAG, "[WIFI] esp_wifi_stop() failed: %d", stop_ret);
                }
            }
            
            button_enable_wakeup();
            
            uint32_t sleep_ms = sleep_ticks * portTICK_PERIOD_MS;
            uint64_t sleep_us = ((uint64_t)sleep_ms) * 1000;
            ESP_LOGI(TAG, "[SLEEP] Light sleep for %u ms (button wake enabled)", sleep_ms);
            
            esp_sleep_enable_timer_wakeup(sleep_us);
            esp_light_sleep_start();
            
            button_disable_wakeup();
            
            if (wifi_initialized && button_is_pressed()) {
                ESP_LOGI(TAG, "[WAKE] GPIO (button)");
            } else if (wifi_initialized) {
                ESP_LOGI(TAG, "[WAKE] Timer");
                ESP_LOGI(TAG, "[WIFI] Restarting after timer wake");
                esp_err_t start_ret = esp_wifi_start();
                if (start_ret != ESP_OK) {
                    ESP_LOGW(TAG, "[WIFI] esp_wifi_start() failed: %d", start_ret);
                }
                
                int retry = 0;
                while (!wifi_manager_is_connected() && retry < 30) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    retry++;
                }
                
                if (wifi_manager_is_connected()) {
                    ESP_LOGI(TAG, "[WIFI] Reconnected");
                } else {
                    ESP_LOGW(TAG, "[WIFI] Reconnect timeout");
                }
            }
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Client starting...");

    esp_set_cpu_freq(ESP_CPU_FREQ_80M);
    ESP_LOGI(TAG, "CPU frequency set to 80 MHz");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    int disp_ret = display_init(&display, SDA_PIN, SCL_PIN, SSD1306_I2C_ADDR_0);
    if (disp_ret != 0) {
        ESP_LOGE(TAG, "Display init FAILED");
    }

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
    
    if (strlen(CONFIG_CLIENT_BACKUP_SSID) > 0) {
        wifi_manager_set_backup(CONFIG_CLIENT_BACKUP_SSID, CONFIG_CLIENT_BACKUP_PASS);
        ESP_LOGI(TAG, "Backup WiFi configured: %s", CONFIG_CLIENT_BACKUP_SSID);
    }
    
    ESP_LOGI(TAG, "Server discovery: UDP broadcast on port 5000");

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
        ESP_LOGI(TAG, "  Display: %s", (disp_ret == 0) ? "OK" : "FAILED");
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
        ESP_LOGI(TAG, "  Time source: %s", sntp_done ? "NTP" : "none");
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
