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
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"
#include "esp_sleep.h"

static const char *TAG = "client";

#define SDA_PIN 4
#define SCL_PIN 5

#define SENSOR_READ_INTERVAL_MS 300000
#define WIFI_SYNC_INTERVAL_MS 300000
#define DISPLAY_ON_MS 12000
#define DISPLAY_SCREEN_INTERVAL_MS 4000

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

/* Secondary SSID/pass. Priority: wifi.env (WIFI_SSID_2 / WIFI_PASS_2) >
 * Kconfig backup (CONFIG_CLIENT_BACKUP_SSID / CONFIG_CLIENT_BACKUP_PASS). */
#ifdef WIFI_SSID_2
#define CLIENT_WIFI_SSID_2 WIFI_SSID_2
#else
#define CLIENT_WIFI_SSID_2 CONFIG_CLIENT_BACKUP_SSID
#endif

#ifdef WIFI_PASS_2
#define CLIENT_WIFI_PASS_2 WIFI_PASS_2
#else
#define CLIENT_WIFI_PASS_2 CONFIG_CLIENT_BACKUP_PASS
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

/* Client connection states. */
typedef enum {
    STATE_DISCOVERING,
    STATE_CONNECTED,
    STATE_AUTONOMOUS
} client_state_t;
static ens160_data_t last_ens_data = {0};

static uint32_t client_uptime_sec = 0;
static int display_screen = 0;
static uint32_t s_planned_sleep_ms = 0;
static bool s_wifi_initialized = false;
static void update_display(void) {
    display_clear_fb(&display);

    int t_int = (int)last_aht_data.temperature;
    int t_dec = (int)(last_aht_data.temperature * 10) % 10;
    if (t_dec < 0) t_dec = -t_dec;

    bool basic_mode = wifi_sync_is_basic_mode();

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
    } else if (display_screen == 1) {
        if (basic_mode) {
            const char *name = wifi_sync_get_assigned_name();
            const char *display_name = (name && name[0]) ? name : "not_defined";
            int char_w = 12;
            int w = strlen(display_name) * char_w;
            int x = (w > 128) ? 0 : (128 - w) / 2;
            display_draw_string_scaled(&display, display_font, x, 10, display_name, 2);
        } else {
            char line1[16], line2[16];
            snprintf(line1, sizeof(line1), "CO2 %u", last_ens_data.eco2);
            snprintf(line2, sizeof(line2), "VOC %u A%u", last_ens_data.tvoc, last_ens_data.aqi);
            display_draw_string_scaled(&display, display_font, 0, 0, line1, 2);
            display_draw_string_scaled(&display, display_font, 0, 17, line2, 2);
        }
    } else {
        const char *name = wifi_sync_get_assigned_name();
        const char *display_name = (name && name[0]) ? name : "not_defined";
        int char_w = 12;
        int w = strlen(display_name) * char_w;
        int x = (w > 128) ? 0 : (128 - w) / 2;
        display_draw_string_scaled(&display, display_font, x, 10, display_name, 2);
    }
    display_present(&display);
}

static void client_adjust_time_after_sleep(uint32_t sleep_ms) {
    if (sleep_ms == 0) return;
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return;
    }
    uint32_t add_sec = sleep_ms / 1000;
    uint32_t add_usec = (sleep_ms % 1000) * 1000;
    tv.tv_usec += add_usec;
    if ((uint32_t)tv.tv_usec >= 1000000) {
        tv.tv_sec++;
        tv.tv_usec -= 1000000;
    }
    tv.tv_sec += add_sec;
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "[TIME] Adjusted by %u ms (was %lu, now %lu)",
             sleep_ms, (unsigned long)(tv.tv_sec - add_sec), (unsigned long)tv.tv_sec);
}

static void client_sync_time_with_server(void) {
    if (!wifi_manager_is_connected()) return;
    uint32_t ts = 0;
    esp_err_t ret = wifi_sync_get_server_time(&ts);
    if (ret == ESP_OK && ts > 0) {
        struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "[TIME] Synced from server: %lu", (unsigned long)ts);
    } else {
        ESP_LOGW(TAG, "[TIME] Server time sync failed: %d", ret);
    }
}

static void client_show_sensor_error(void) {
    display_clear_fb(&display);
    display_draw_string_scaled(&display, display_font, 16, 0, "SENSOR", 2);
    display_draw_string_scaled(&display, display_font, 28, 17, "ERROR", 2);
    display_present(&display);
    display_on(&display);
}

static bool read_sensors_and_store(void) {
    ESP_LOGI(TAG, "[SENSORS] Reading...");

    aht21_data_t aht_data = {0};
    ens160_data_t ens_data = {0};
    bool basic_mode = wifi_sync_is_basic_mode();
    bool aht_ok = false;
    bool ens_ok = false;

    int aht_ret = aht21_read(I2C_NUM_0, &aht_data);
    esp_task_wdt_reset();
    if (aht_ret == 0) {
        last_aht_data = aht_data;
        aht_ok = true;
        int t_int = (int)aht_data.temperature;
        int t_dec = (int)(aht_data.temperature * 10) % 10;
        if (t_dec < 0) t_dec = -t_dec;
        ESP_LOGI(TAG, "[SENSORS] AHT21: T=%d.%d H=%d", t_int, t_dec, (int)aht_data.humidity);
    } else {
        ESP_LOGW(TAG, "[SENSORS] AHT21 failed: %d", aht_ret);
    }

#if ENS160_ENABLE
    if (basic_mode) {
        last_ens_data = (ens160_data_t){0};
        ESP_LOGI(TAG, "[SENSORS] ENS160 skipped (basic_mode)");
    } else {
        int ens_ret = ens160_read(I2C_NUM_0, &ens_data);
        esp_task_wdt_reset();
        if (ens_ret == 0) {
            last_ens_data = ens_data;
            ens_ok = true;
            ESP_LOGI(TAG, "[SENSORS] ENS160: eCO2=%u TVOC=%u AQI=%u", ens_data.eco2, ens_data.tvoc, ens_data.aqi);
        } else {
            ESP_LOGW(TAG, "[SENSORS] ENS160 failed: %d", ens_ret);
        }

        ens160_set_env(I2C_NUM_0, last_aht_data.temperature, last_aht_data.humidity);
        esp_task_wdt_reset();
    }
#else
    (void)ens_data;
    ESP_LOGI(TAG, "[SENSORS] ENS160 disabled");
#endif

    client_uptime_sec = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

    time_t now_ts = time(NULL);
    if (now_ts <= 0) {
        now_ts = (time_t)client_uptime_sec;
    }

    client_record_t record = {
        .timestamp = now_ts,
        .uptime_sec = client_uptime_sec,
        .temperature = last_aht_data.temperature,
        .humidity = last_aht_data.humidity,
        .eco2 = last_ens_data.eco2,
        .tvoc = last_ens_data.tvoc,
        .aqi = last_ens_data.aqi,
        .synced = false
    };

    if (data_storage_append(&record) == 0) {
        ESP_LOGI(TAG, "[STORAGE] Saved ts=%ld up=%u, total=%d",
                 (long)record.timestamp, record.uptime_sec,
                 data_storage_get_count());
    } else {
        ESP_LOGW(TAG, "[STORAGE] Append failed, SPIFFS may be full");
    }

    if (basic_mode) {
        return aht_ok;
    }
    return aht_ok && ens_ok;
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

static esp_err_t client_bootstrap_wifi(void) {
    if (s_wifi_initialized && wifi_manager_is_connected() && wifi_sync_get_server_ip() != NULL) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "[WIFI] Bootstrapping connection for cycle");
    esp_err_t ret = wifi_sync_connect_to_server(CLIENT_WIFI_SSID, CLIENT_WIFI_PASS,
                                                CLIENT_WIFI_SSID_2, CLIENT_WIFI_PASS_2);
    s_wifi_initialized = (ret == ESP_OK);
    return ret;
}

static esp_err_t client_perform_upload_cycle(bool from_button);

static void client_turn_off_wifi_and_reset_state(TickType_t now) {
    if (wifi_manager_is_connected() || wifi_sync_get_server_ip() != NULL) {
        ESP_LOGI(TAG, "[WIFI] Stopping after upload cycle");
        esp_err_t stop_ret = esp_wifi_stop();
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "[WIFI] esp_wifi_stop() failed: %d", stop_ret);
        }
    }
}

static esp_err_t client_perform_upload_cycle(bool from_button) {
    esp_err_t ret = client_bootstrap_wifi();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[CYCLE] WiFi bootstrap failed");
        return ret;
    }
    if (!wifi_manager_is_connected()) {
        ret = client_ensure_wifi_connected();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[CYCLE] WiFi not available");
            return ret;
        }
    }
    client_sync_time_with_server();
    ret = wifi_sync_upload_unsynced();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[CYCLE] Upload OK");
    } else {
        ESP_LOGW(TAG, "[CYCLE] Upload failed: %d", ret);
    }
    return ret;
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
    bool display_active = false;
    TickType_t display_on_until = 0;
    int local_display_screen = 0;
    int initial_polls_remaining = 6;
    bool woke_by_button = false;
    bool woke_by_timer = false;

    display_off(&display);

    ESP_LOGI(TAG, "[TASK] Client task started #%d (client_id=%s)", task_start_count, CONFIG_CLIENT_ID);

    while (1) {
        TickType_t now = xTaskGetTickCount();
        esp_task_wdt_reset();

        bool button_pressed = button_is_pressed_debounced();
        if (button_pressed && !woke_by_button) {
            ESP_LOGI(TAG, "[BUTTON] Detected press (debounced)");
        }

        if (display_active) {
            if (now >= display_on_until) {
                display_active = false;
                local_display_screen = 0;
                display_screen = 0;
                display_off(&display);
                ESP_LOGI(TAG, "[DISPLAY] Timeout, OFF");
            } else if ((now - last_screen_switch) >= pdMS_TO_TICKS(DISPLAY_SCREEN_INTERVAL_MS)) {
                int max_screens = wifi_sync_is_basic_mode() ? 2 : 3;
                local_display_screen = (local_display_screen + 1) % max_screens;
                display_screen = local_display_screen;
                update_display();
                last_screen_switch = now;
            }
        }

        if (woke_by_button) {
            ESP_LOGI(TAG, "[CYCLE] Button wake: read + upload + sync time");
            bool sensor_ok = read_sensors_and_store();
            last_sensor_read = now;
            if (!sensor_ok) {
                ESP_LOGE(TAG, "[CYCLE] Sensor read failed");
                client_show_sensor_error();
                vTaskDelay(pdMS_TO_TICKS(4000));
                display_active = false;
                local_display_screen = 0;
                display_screen = 0;
                display_off(&display);
                ESP_LOGI(TAG, "[CYCLE] Going back to sleep after sensor error");
            } else {
                display_active = true;
                local_display_screen = 0;
                display_screen = 0;
                last_screen_switch = now;
                int max_screens = wifi_sync_is_basic_mode() ? 2 : 3;
                display_on_until = now + pdMS_TO_TICKS(DISPLAY_SCREEN_INTERVAL_MS * max_screens);
                display_on(&display);
                update_display();
                vTaskDelay(pdMS_TO_TICKS(DISPLAY_SCREEN_INTERVAL_MS));

                esp_err_t cycle_ret = client_perform_upload_cycle(true);
                last_upload_attempt = now;

                if (cycle_ret == ESP_OK) {
                    ESP_LOGI(TAG, "[CYCLE] Upload OK, server time applied");
                } else {
                    ESP_LOGW(TAG, "[CYCLE] Upload failed (%d), data kept locally", cycle_ret);
                }

                local_display_screen = (local_display_screen + 1) % max_screens;
                display_screen = local_display_screen;
                last_screen_switch = now;
                update_display();
                vTaskDelay(pdMS_TO_TICKS(DISPLAY_SCREEN_INTERVAL_MS));

                display_active = false;
                local_display_screen = 0;
                display_screen = 0;
                display_off(&display);
            }

            client_turn_off_wifi_and_reset_state(now);
            state = STATE_AUTONOMOUS;
            state_entered = now;
            last_discovery_attempt = now;
            s_wifi_initialized = false;
            upload_failures = 0;

            woke_by_button = false;
            s_planned_sleep_ms = SENSOR_READ_INTERVAL_MS;
            esp_sleep_enable_timer_wakeup((uint64_t)s_planned_sleep_ms * 1000);
            ESP_LOGI(TAG, "[SLEEP] After button cycle, timer sleep for %u ms", s_planned_sleep_ms);
            button_enable_wakeup();
            esp_light_sleep_start();
            button_disable_wakeup();
            client_adjust_time_after_sleep(s_planned_sleep_ms);

            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (woke_by_timer) {
            ESP_LOGI(TAG, "[CYCLE] Timer wake: read + upload + sync time");
            read_sensors_and_store();
            last_sensor_read = now;
            esp_err_t cycle_ret = client_perform_upload_cycle(false);
            last_upload_attempt = now;

            if (cycle_ret == ESP_OK) {
                ESP_LOGI(TAG, "[CYCLE] Timer cycle OK, server time applied");
            } else {
                ESP_LOGW(TAG, "[CYCLE] Timer cycle upload failed (%d), data kept locally", cycle_ret);
            }

            client_turn_off_wifi_and_reset_state(now);
            state = STATE_AUTONOMOUS;
            state_entered = now;
            last_discovery_attempt = now;
            s_wifi_initialized = false;
            upload_failures = 0;

            woke_by_timer = false;
            s_planned_sleep_ms = SENSOR_READ_INTERVAL_MS;
            esp_sleep_enable_timer_wakeup((uint64_t)s_planned_sleep_ms * 1000);
            ESP_LOGI(TAG, "[SLEEP] After timer cycle, timer sleep for %u ms", s_planned_sleep_ms);
            button_enable_wakeup();
            esp_light_sleep_start();
            button_disable_wakeup();
            client_adjust_time_after_sleep(s_planned_sleep_ms);

            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
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
                if (!s_wifi_initialized) {
                    ESP_LOGI(TAG, "[STATE] DISCOVERING: starting WiFi");
                    esp_err_t ret = wifi_sync_connect_to_server(CLIENT_WIFI_SSID, CLIENT_WIFI_PASS,
                                                                CLIENT_WIFI_SSID_2, CLIENT_WIFI_PASS_2);
                    s_wifi_initialized = true;
                    if (ret == ESP_OK) {
                        state = STATE_CONNECTED;
                        state_entered = now;
                        upload_failures = 0;
                        ESP_LOGI(TAG, "[STATE] -> CONNECTED");
                        client_sync_time_with_server();
                        wifi_sync_upload_unsynced();
                        last_upload_attempt = now;
                    }
                } else if (wifi_sync_get_server_ip() != NULL) {
                    state = STATE_CONNECTED;
                    state_entered = now;
                    upload_failures = 0;
                    ESP_LOGI(TAG, "[STATE] -> CONNECTED (server found)");
                    client_sync_time_with_server();
                    wifi_sync_upload_unsynced();
                    last_upload_attempt = now;
                } else if ((now - state_entered) >= pdMS_TO_TICKS(CONFIG_CLIENT_DISCOVER_TIMEOUT_MS)) {
                    ESP_LOGW(TAG, "[STATE] DISCOVERING timeout, -> AUTONOMOUS");
                    state = STATE_AUTONOMOUS;
                    state_entered = now;
                    last_discovery_attempt = now;
                    if (s_wifi_initialized) {
                        esp_wifi_stop();
                        s_wifi_initialized = false;
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
                        client_sync_time_with_server();
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
                            s_wifi_initialized = false;
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
                    s_wifi_initialized = false;
                }
                break;
        }

        if (button_pressed && !display_active) {
            ESP_LOGI(TAG, "[BUTTON] Turning display ON");
            display_active = true;
            int max_screens = wifi_sync_is_basic_mode() ? 2 : 3;
            uint32_t on_ms = DISPLAY_SCREEN_INTERVAL_MS * max_screens;
            display_on_until = now + pdMS_TO_TICKS(on_ms);
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
                if (state == STATE_AUTONOMOUS && s_wifi_initialized) {
                    ESP_LOGI(TAG, "[WIFI] Stopping for light sleep");
                    esp_err_t stop_ret = esp_wifi_stop();
                    if (stop_ret != ESP_OK) {
                        ESP_LOGW(TAG, "[WIFI] esp_wifi_stop() failed: %d", stop_ret);
                    }
                }

                button_enable_wakeup();

                uint32_t sleep_ms = sleep_ticks * portTICK_PERIOD_MS;
                s_planned_sleep_ms = sleep_ms;
                uint64_t sleep_us = ((uint64_t)sleep_ms) * 1000;
                ESP_LOGI(TAG, "[SLEEP] Light sleep for %u ms (button wake enabled)", sleep_ms);

                esp_sleep_enable_timer_wakeup(sleep_us);
                esp_light_sleep_start();

                int wake_gpio = gpio_get_level((gpio_num_t)BUTTON_GPIO);
                ESP_LOGI(TAG, "[WAKE] raw GPIO=%d isr_count=%u", wake_gpio, button_get_isr_count());

                button_disable_wakeup();
                client_adjust_time_after_sleep(s_planned_sleep_ms);

                bool button_down = button_is_pressed();
                if (button_down) {
                    button_set_pressed_flag();
                    ESP_LOGI(TAG, "[WAKE] GPIO (button)");
                    woke_by_button = true;
                } else {
                    ESP_LOGI(TAG, "[WAKE] Timer");
                    woke_by_timer = true;
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
        ESP_LOGI(TAG, "  Primary SSID: %s", CLIENT_WIFI_SSID);
        if (strlen(CLIENT_WIFI_SSID_2) > 0) {
            ESP_LOGI(TAG, "  Secondary SSID: %s", CLIENT_WIFI_SSID_2);
        }
        ESP_LOGI(TAG, "  Time source: %s", (time(NULL) > 0) ? "server" : "none");
        ESP_LOGI(TAG, "  Basic mode: %s", wifi_sync_is_basic_mode() ? "true" : "false");
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
