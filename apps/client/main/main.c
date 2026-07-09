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

/* Priority: wifi.env compile definitions > Kconfig defaults */
#ifdef WIFI_SSID
#define CLIENT_WIFI_SSID WIFI_SSID
#else
#define CLIENT_WIFI_SSID CONFIG_CLIENT_WIFI_SSID
#endif

#ifdef WIFI_PASS
#define CLIENT_WIFI_PASS WIFI_PASS
#else
#define CLIENT_WIFI_PASS CONFIG_CLIENT_WIFI_PASS
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

#ifndef CONFIG_CLIENT_RETRY_INTERVAL_MS
#define CONFIG_CLIENT_RETRY_INTERVAL_MS 3600000
#endif

#ifndef CONFIG_CLIENT_DEFAULT_SYNC_INTERVAL_SEC
#define CONFIG_CLIENT_DEFAULT_SYNC_INTERVAL_SEC 600
#endif

static Display display;
static const font_info_t *display_font = NULL;
static TaskHandle_t client_task_handle = NULL;

static aht21_data_t last_aht_data = {0};

/* Set by wifi_sync.c when the server assigns a new name to this client. */
bool g_name_updated = false;

static uint32_t g_name_show_start_ms = 0;

/* Client connection states. */
typedef enum {
    STATE_DISCOVERING,
    STATE_CONNECTED,
    STATE_AUTONOMOUS
} client_state_t;
static ens160_data_t last_ens_data = {0};

static uint32_t client_uptime_sec = 0;
static int display_screen = 0;
static void update_display(void) {
    display_clear_fb(&display);

    /* Start showing the assigned name when it is first received. */
    if (g_name_updated) {
        g_name_updated = false;
        g_name_show_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (g_name_show_start_ms != 0 && (now_ms - g_name_show_start_ms) < 5000) {
        const char *name = wifi_sync_get_assigned_name();
        if (name && name[0]) {
            int char_w = 12;
            int w = strlen(name) * char_w;
            int x = (w > 128) ? 0 : (128 - w) / 2;
            display_draw_string_scaled(&display, display_font, x, 10, name, 2);
            display_present(&display);
            return;
        }
    } else {
        g_name_show_start_ms = 0;
    }

    int t_int = (int)last_aht_data.temperature;
    int t_dec = (int)(last_aht_data.temperature * 10) % 10;
    if (t_dec < 0) t_dec = -t_dec;

    if (display_screen == 0) {
        char line1[16], line2[16];
        snprintf(line1, sizeof(line1), "%d.%dC %d%%", t_int, t_dec, (int)last_aht_data.humidity);
        time_t now = time(NULL);
        if (now > 0) {
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

static void read_sensors_and_store(void) {
    ESP_LOGI(TAG, "[SENSORS] Reading...");

    aht21_data_t aht_data = {0};
    ens160_data_t ens_data = {0};

    int aht_ret = aht21_read(I2C_NUM_0, &aht_data);
    esp_task_wdt_reset();
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
    esp_task_wdt_reset();
    if (ens_ret == 0) {
        last_ens_data = ens_data;
        ESP_LOGI(TAG, "[SENSORS] ENS160: eCO2=%u TVOC=%u AQI=%u", ens_data.eco2, ens_data.tvoc, ens_data.aqi);
    } else {
        ESP_LOGW(TAG, "[SENSORS] ENS160 failed: %d", ens_ret);
    }

    ens160_set_env(I2C_NUM_0, last_aht_data.temperature, last_aht_data.humidity);
    esp_task_wdt_reset();
#else
    (void)ens_data;
    ESP_LOGI(TAG, "[SENSORS] ENS160 disabled");
#endif

    client_uptime_sec = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

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
}

static esp_err_t client_ensure_wifi_connected(void) {
    if (wifi_manager_is_connected()) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "[WIFI] Starting for upload");
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[WIFI] esp_wifi_start() failed: %d", ret);
    }
    int retry = 0;
    while (!wifi_manager_is_connected() && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();
        retry++;
    }
    return wifi_manager_is_connected() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void client_task(void *pvParameters) {
    (void)pvParameters;
    static int task_start_count = 0;
    task_start_count++;

    TickType_t now_init = xTaskGetTickCount();
    TickType_t last_sensor_read = now_init;
    TickType_t last_upload_attempt = 0;
    TickType_t last_discovery_attempt = now_init;
    TickType_t state_entered = now_init;
    TickType_t last_screen_switch = 0;

    client_state_t state = STATE_DISCOVERING;
    int upload_failures = 0;
    bool wifi_initialized = false;
    bool display_active = false;
    TickType_t display_on_until = 0;
    int local_display_screen = 0;
    int initial_polls_remaining = 6;

    display_off(&display);

    ESP_LOGI(TAG, "[TASK] Client task started #%d (client_id=%s)", task_start_count, CONFIG_CLIENT_ID);

    while (1) {
        TickType_t now = xTaskGetTickCount();
        esp_task_wdt_reset();

        bool button_pressed = button_is_pressed_debounced();
        if (button_pressed) {
            ESP_LOGI(TAG, "[BUTTON] Detected press (debounced)");
        }

        if (display_active) {
            if (now >= display_on_until) {
                display_active = false;
                display_off(&display);
                ESP_LOGI(TAG, "[DISPLAY] Timeout, OFF");
            } else if ((now - last_screen_switch) >= pdMS_TO_TICKS(DISPLAY_SCREEN_INTERVAL_MS)) {
                local_display_screen = !local_display_screen;
                display_screen = local_display_screen;
                update_display();
                last_screen_switch = now;
            }
        }

        if ((now - last_sensor_read) >= pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS)) {
            read_sensors_and_store();
            last_sensor_read = now;
            if (display_active) {
                update_display();
            }
        }

        switch (state) {
            case STATE_DISCOVERING:
                if (!wifi_initialized) {
                    ESP_LOGI(TAG, "[STATE] DISCOVERING: starting WiFi");
                    esp_err_t ret = wifi_sync_connect_to_server(CLIENT_WIFI_SSID, CLIENT_WIFI_PASS);
                    wifi_initialized = true;
                    if (ret == ESP_OK) {
                        state = STATE_CONNECTED;
                        state_entered = now;
                        upload_failures = 0;
                        ESP_LOGI(TAG, "[STATE] -> CONNECTED");
                        wifi_sync_upload_unsynced();
                        last_upload_attempt = now;
                    }
                } else if (wifi_sync_get_server_ip() != NULL) {
                    state = STATE_CONNECTED;
                    state_entered = now;
                    upload_failures = 0;
                    ESP_LOGI(TAG, "[STATE] -> CONNECTED (server found)");
                    wifi_sync_upload_unsynced();
                    last_upload_attempt = now;
                } else if ((now - state_entered) >= pdMS_TO_TICKS(CONFIG_CLIENT_DISCOVER_TIMEOUT_MS)) {
                    ESP_LOGW(TAG, "[STATE] DISCOVERING timeout, -> AUTONOMOUS");
                    state = STATE_AUTONOMOUS;
                    state_entered = now;
                    last_discovery_attempt = now;
                    if (wifi_initialized) {
                        esp_wifi_stop();
                        wifi_initialized = false;
                    }
                }
                break;

            case STATE_CONNECTED: {
                uint32_t sync_interval_ms = wifi_sync_get_sync_interval() * 1000;
                if (sync_interval_ms < 60000) sync_interval_ms = 60000;
                if ((now - last_upload_attempt) >= pdMS_TO_TICKS(sync_interval_ms)) {
                    ESP_LOGI(TAG, "[STATE] CONNECTED: uploading...");
                    esp_err_t ret = client_ensure_wifi_connected();
                    if (ret == ESP_OK) {
                        ret = wifi_sync_upload_unsynced();
                    }
                    last_upload_attempt = now;
                    if (ret == ESP_OK) {
                        upload_failures = 0;
                        ESP_LOGI(TAG, "[STATE] upload OK");
                    } else {
                        upload_failures++;
                        ESP_LOGW(TAG, "[STATE] upload failed (%d)", upload_failures);
                        if (upload_failures >= 3) {
                            ESP_LOGW(TAG, "[STATE] too many failures, -> AUTONOMOUS");
                            state = STATE_AUTONOMOUS;
                            state_entered = now;
                            last_discovery_attempt = now;
                            esp_wifi_stop();
                            wifi_initialized = false;
                        }
                    }
                }
                break;
            }

            case STATE_AUTONOMOUS:
                if ((now - last_discovery_attempt) >= pdMS_TO_TICKS(CONFIG_CLIENT_RETRY_INTERVAL_MS)) {
                    ESP_LOGI(TAG, "[STATE] AUTONOMOUS: retry discovery");
                    state = STATE_DISCOVERING;
                    state_entered = now;
                    wifi_initialized = false;
                }
                break;
        }

        if (button_pressed && !display_active) {
            ESP_LOGI(TAG, "[BUTTON] Turning display ON");
            display_active = true;
            display_on_until = now + pdMS_TO_TICKS(DISPLAY_ON_MS);
            local_display_screen = 0;
            display_screen = 0;
            last_screen_switch = now;
            display_on(&display);
            update_display();
            button_was_pressed();
        }

        TickType_t next_sensor = pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS) - (now - last_sensor_read);
        TickType_t sleep_ticks = next_sensor;

        if (state == STATE_CONNECTED) {
            uint32_t sync_interval_ms = wifi_sync_get_sync_interval() * 1000;
            if (sync_interval_ms < 60000) sync_interval_ms = 60000;
            TickType_t next_upload = pdMS_TO_TICKS(sync_interval_ms) - (now - last_upload_attempt);
            if (next_upload < sleep_ticks) sleep_ticks = next_upload;
        }

        if (state == STATE_AUTONOMOUS) {
            TickType_t next_discovery = pdMS_TO_TICKS(CONFIG_CLIENT_RETRY_INTERVAL_MS) - (now - last_discovery_attempt);
            if (next_discovery < sleep_ticks) sleep_ticks = next_discovery;
        }

        if (display_active) {
            TickType_t display_remaining = display_on_until - now;
            if (display_remaining < sleep_ticks) sleep_ticks = display_remaining;
        }

        if (initial_polls_remaining > 0) {
            sleep_ticks = pdMS_TO_TICKS(5000);
            initial_polls_remaining--;
        }

        if (state == STATE_DISCOVERING) {
            sleep_ticks = pdMS_TO_TICKS(1000);
        }

        if (sleep_ticks > pdMS_TO_TICKS(10)) {
            static bool last_sleep_skip_logged = false;
            if (button_is_pressed()) {
                if (!last_sleep_skip_logged) {
                    ESP_LOGI(TAG, "[SLEEP] Button held, skipping light sleep");
                    last_sleep_skip_logged = true;
                }
            } else {
                last_sleep_skip_logged = false;
                /* In CONNECTED state keep WiFi on so the next upload can start
                 * immediately after wake.  Stop WiFi only in AUTONOMOUS mode. */
                if (state == STATE_AUTONOMOUS && wifi_initialized) {
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

                int wake_gpio = gpio_get_level((gpio_num_t)BUTTON_GPIO);
                ESP_LOGI(TAG, "[WAKE] raw GPIO=%d isr_count=%u", wake_gpio, button_get_isr_count());

                button_disable_wakeup();

                bool woke_by_button = button_is_pressed();
                if (woke_by_button) {
                    button_set_pressed_flag();
                    ESP_LOGI(TAG, "[WAKE] GPIO (button)");
                } else if (state == STATE_AUTONOMOUS && !wifi_manager_is_connected()) {
                    ESP_LOGI(TAG, "[WAKE] Timer");
                    ESP_LOGI(TAG, "[WIFI] Restarting after timer wake");
                    esp_err_t start_ret = esp_wifi_start();
                    if (start_ret != ESP_OK) {
                        ESP_LOGW(TAG, "[WIFI] esp_wifi_start() failed: %d", start_ret);
                    }

                    int retry = 0;
                    while (!wifi_manager_is_connected() && retry < 30) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_task_wdt_reset();
                        retry++;
                    }

                    if (wifi_manager_is_connected()) {
                        ESP_LOGI(TAG, "[WIFI] Reconnected");
                    } else {
                        ESP_LOGW(TAG, "[WIFI] Reconnect timeout");
                    }
                } else {
                    ESP_LOGI(TAG, "[WAKE] Timer");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
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

    // Read sensors once to initialize display data
    aht21_data_t aht_data = {0};
    ens160_data_t ens_data = {0};
    if (aht21_read(I2C_NUM_0, &aht_data) == 0) {
        last_aht_data = aht_data;
        int t_int = (int)aht_data.temperature;
        int t_dec = (int)(aht_data.temperature * 10) % 10;
        if (t_dec < 0) t_dec = -t_dec;
        ESP_LOGI(TAG, "Initial sensor read: T=%d.%d H=%d", t_int, t_dec, (int)aht_data.humidity);
    }
#if ENS160_ENABLE
    if (ens160_read(I2C_NUM_0, &ens_data) == 0) {
        last_ens_data = ens_data;
        ESP_LOGI(TAG, "Initial sensor read: CO2=%u TVOC=%u AQI=%u", ens_data.eco2, ens_data.tvoc, ens_data.aqi);
    }
#endif

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
        ESP_LOGI(TAG, "  Time source: %s", (time(NULL) > 0) ? "server" : "none");
        ESP_LOGI(TAG, "  Heap at init: %d KB", (int)(esp_get_free_heap_size() / 1024));
        ESP_LOGI(TAG, "=== END SELF-TEST ===");
    }

    // Create client task with 4KB stack
    xTaskCreate(client_task, "client_task", 8192, NULL, 5, &client_task_handle);
    button_set_task_handle(client_task_handle);
    
    ESP_LOGI(TAG, "Client task created");
    
    // Main loop just idles
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
