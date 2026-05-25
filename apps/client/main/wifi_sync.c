#include "wifi_sync.h"
#include "data_storage.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_sync";
static char s_client_id[32] = {0};
static char s_server_url[128] = {0};

esp_err_t wifi_sync_init(const char *client_id) {
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
    ESP_LOGI(TAG, "WiFi sync initialized for client: %s", s_client_id);
    return ESP_OK;
}

esp_err_t wifi_sync_connect_to_server(const char *ap_ssid, const char *ap_pass) {
    ESP_LOGI(TAG, "Connecting to server AP: %s", ap_ssid);
    
    esp_err_t ret = wifi_manager_init_sta(ap_ssid, ap_pass);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to AP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    int retry = 0;
    while (!wifi_manager_is_connected() && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
    
    if (!wifi_manager_is_connected()) {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    snprintf(s_server_url, sizeof(s_server_url), "http://192.168.4.1/api/upload?id=%s", s_client_id);
    
    ESP_LOGI(TAG, "Server URL: %s", s_server_url);
    return ESP_OK;
}

esp_err_t wifi_sync_upload_unsynced(void) {
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    client_record_t records[5];  // Small batch
    int count = data_storage_read_unsynced(records, 5);
    
    if (count == 0) {
        ESP_LOGI(TAG, "No unsynced records");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Uploading %d records", count);
    
    // Build JSON manually to save memory
    char json_buf[1024];
    int offset = 0;
    offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, "{\"readings\":[");
    
    for (int i = 0; i < count; i++) {
        if (i > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
        int t_int = (int)records[i].temperature;
        int t_dec = (int)(records[i].temperature * 10) % 10;
        if (t_dec < 0) t_dec = -t_dec;
        offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
            "{\"ts\":%ld,\"up\":%u,\"t\":%d.%d,\"h\":%d,\"c\":%u,\"v\":%u,\"a\":%u}",
            (long)records[i].timestamp, records[i].uptime_sec,
            t_int, t_dec, (int)records[i].humidity,
            records[i].eco2, records[i].tvoc, records[i].aqi);
    }
    
    offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, "]}");
    
    esp_http_client_config_t config = {
        .url = s_server_url,
        .method = HTTP_METHOD_POST,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));
    
    esp_err_t ret = esp_http_client_perform(client);
    int status = 0;
    
    if (ret == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "Upload OK, status=%d", status);
            data_storage_mark_synced(count);
        } else {
            ESP_LOGW(TAG, "Upload failed, status=%d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(ret));
    }
    
    esp_http_client_cleanup(client);
    
    if (ret != ESP_OK) return ret;
    return (status == 200) ? ESP_OK : ESP_FAIL;
}

void wifi_sync_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from server");
}

bool wifi_sync_is_connected(void) {
    return wifi_manager_is_connected();
}
