#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"
#include "wifi_manager.h"
#include "data_store.h"
#include "client_registry.h"
#include "http_server.h"

static const char *TAG = "server";

#define WIFI_AP_SSID "AirMon-Server"
#define WIFI_AP_PASS "12345678"
#define WIFI_STA_SSID NULL
#define WIFI_STA_PASS NULL
#define STA_TIMEOUT_SEC 30
#define STALE_TIMEOUT_SEC 360
#define MAIN_LOOP_INTERVAL_MS 30000

static void init_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for SNTP sync (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    
    if (timeinfo.tm_year >= (2020 - 1900)) {
        ESP_LOGI(TAG, "SNTP synced: %s", asctime(&timeinfo));
    } else {
        ESP_LOGW(TAG, "SNTP sync failed, time may be inaccurate");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Server starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    if (data_store_init() != 0) {
        ESP_LOGE(TAG, "Failed to init data store");
        return;
    }
    ESP_LOGI(TAG, "Data store initialized");

    client_registry_init();
    int loaded = client_registry_load();
    ESP_LOGI(TAG, "Client registry loaded %d clients", loaded);

    ESP_ERROR_CHECK(wifi_manager_init_ap_with_sta_fallback(
        WIFI_AP_SSID, WIFI_AP_PASS,
        WIFI_STA_SSID, WIFI_STA_PASS,
        STA_TIMEOUT_SEC));
    vTaskDelay(pdMS_TO_TICKS(1000));

    char ip_str[16] = {0};
    if (wifi_manager_is_connected()) {
        wifi_manager_get_ip(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "STA connected, IP: %s", ip_str);
        init_sntp();
    } else {
        wifi_manager_get_ap_ip(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "AP-only mode, AP IP: %s", ip_str);
    }

    if (http_server_init() != 0) {
        ESP_LOGE(TAG, "Failed to init HTTP server");
        return;
    }
    if (http_server_start() != 0) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }
    ESP_LOGI(TAG, "HTTP server running at http://%s", ip_str);

    ESP_LOGI(TAG, "Server ready. Main loop started.");

    while (1) {
        client_registry_check_stale(STALE_TIMEOUT_SEC);
        esp_task_wdt_reset();
        
        ESP_LOGI(TAG, "Stats: uptime=%ds heap=%dKB clients=%d",
                 (int)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
                 (int)(esp_get_free_heap_size() / 1024),
                 loaded);
        
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_INTERVAL_MS));
    }
}
