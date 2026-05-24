#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_manager.h"

static const char *TAG = "server";

#define WIFI_AP_SSID "ESP8266_Server"
#define WIFI_AP_PASS "12345678"

void app_main(void) {
    ESP_LOGI(TAG, "Server starting...");

    // Initialize WiFi in AP mode
    ESP_ERROR_CHECK(wifi_manager_init_ap(WIFI_AP_SSID, WIFI_AP_PASS));

    // Wait for AP to be ready
    vTaskDelay(pdMS_TO_TICKS(1000));

    char ip_str[16];
    if (wifi_manager_get_ip(ip_str, sizeof(ip_str)) == ESP_OK) {
        ESP_LOGI(TAG, "Server IP: %s", ip_str);
    }

    // TODO: Initialize HTTP server
    // TODO: Initialize data storage (LittleFS)
    // TODO: Start HTTP server with REST API endpoints

    ESP_LOGI(TAG, "Server ready. Waiting for clients...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
