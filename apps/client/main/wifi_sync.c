#include "wifi_sync.h"
#include "data_storage.h"
#include "wifi_manager.h"
#include "ens160.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "tcpip_adapter.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static const char *TAG = "wifi_sync";
static char s_client_id[32] = {0};

#define CLIENT_NVS_NAMESPACE "client"
#define CLIENT_NVS_KEY_NAME  "name"
#define CLIENT_NVS_KEY_BASIC_MODE "basic_mode"

static char s_server_url[128] = {0};
static char s_server_ip[16] = {0};
static bool s_ip_resolved = false;
static char s_assigned_name[32] = {0};
static uint32_t s_sync_interval_sec = 600; /* default 10 minutes */
static bool s_basic_mode = true;

static void load_assigned_name_from_nvs(void) {
    nvs_handle handle;
    esp_err_t err = nvs_open(CLIENT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_assigned_name);
    err = nvs_get_str(handle, CLIENT_NVS_KEY_NAME, s_assigned_name, &len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded assigned name from NVS: %s", s_assigned_name);
    }
    nvs_close(handle);
}

static void save_assigned_name_to_nvs(const char *name) {
    nvs_handle handle;
    esp_err_t err = nvs_open(CLIENT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %d", err);
        return;
    }
    err = nvs_set_str(handle, CLIENT_NVS_KEY_NAME, name);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str failed: %d", err);
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed: %d", err);
    }
    nvs_close(handle);
}

static void load_basic_mode_from_nvs(void) {
    nvs_handle handle;
    esp_err_t err = nvs_open(CLIENT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }
    uint8_t val = 1;
    err = nvs_get_u8(handle, CLIENT_NVS_KEY_BASIC_MODE, &val);
    nvs_close(handle);
    if (err == ESP_OK) {
        s_basic_mode = (val != 0);
        ESP_LOGI(TAG, "Loaded basic_mode from NVS: %s", s_basic_mode ? "true" : "false");
    }
}

static void save_basic_mode_to_nvs(bool value) {
    nvs_handle handle;
    esp_err_t err = nvs_open(CLIENT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %d", err);
        return;
    }
    err = nvs_set_u8(handle, CLIENT_NVS_KEY_BASIC_MODE, value ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_u8 failed: %d", err);
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed: %d", err);
    }
    nvs_close(handle);
}

#define DISCOVER_PORT 5000
#define UPLOAD_BATCH_SIZE 50

static uint32_t get_broadcast_addr(void) {
    tcpip_adapter_ip_info_t info;
    if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &info) == ESP_OK) {
        uint32_t ip = ntohl(info.ip.addr);
        uint32_t mask = ntohl(info.netmask.addr);
        uint32_t bcast = ip | (~mask);
        return htonl(bcast);
    }
    return htonl(INADDR_BROADCAST);
}

esp_err_t wifi_sync_init(const char *client_id) {
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
    s_server_ip[0] = '\0';
    s_ip_resolved = false;
    s_assigned_name[0] = '\0';
    s_sync_interval_sec = 600;
    s_basic_mode = true;
    load_assigned_name_from_nvs();
    load_basic_mode_from_nvs();
#ifdef SERVER_IP
    strncpy(s_server_ip, SERVER_IP, sizeof(s_server_ip) - 1);
    s_ip_resolved = true;
    snprintf(s_server_url, sizeof(s_server_url), "http://%s/api/upload?id=%s", s_server_ip, s_client_id);
    ESP_LOGI(TAG, "WiFi sync initialized for client: %s", s_client_id);
    ESP_LOGI(TAG, "Fallback server IP configured: %s", s_server_ip);
#else
    ESP_LOGI(TAG, "WiFi sync initialized for client: %s", s_client_id);
#endif
    return ESP_OK;
}

esp_err_t wifi_sync_discover_server(uint32_t timeout_ms) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Discovery socket create failed");
        return ESP_FAIL;
    }

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGW(TAG, "Discovery setsockopt SO_BROADCAST failed");
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVER_PORT);
    dest.sin_addr.s_addr = get_broadcast_addr();

    const char *discover_msg = "AIRMON_DISCOVER";
    char buf[64];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    /* Send several broadcasts; some routers drop the first UDP broadcast packet. */
    const int probes = 3;
    uint32_t per_probe_timeout = timeout_ms / probes;
    if (per_probe_timeout < 3000) per_probe_timeout = 3000;

    for (int p = 0; p < probes; p++) {
        int sent = sendto(sock, discover_msg, strlen(discover_msg), 0,
                          (struct sockaddr *)&dest, sizeof(dest));
        if (sent < 0) {
            ESP_LOGE(TAG, "Discovery broadcast failed");
            close(sock);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Discovery broadcast #%d sent to %s", p + 1,
                 inet_ntoa(dest.sin_addr));

        struct timeval tv;
        tv.tv_sec = per_probe_timeout / 1000;
        tv.tv_usec = (per_probe_timeout % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           (struct sockaddr *)&from, &fromlen);
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, "Discovery response: %s", buf);
            if (strncmp(buf, "AIRMON_RESPONSE ", 16) == 0) {
                char *ip = buf + 16;
                char *nl = strchr(ip, '\n');
                if (nl) *nl = '\0';
                char *cr = strchr(ip, '\r');
                if (cr) *cr = '\0';
                strncpy(s_server_ip, ip, sizeof(s_server_ip) - 1);
                s_ip_resolved = true;
                snprintf(s_server_url, sizeof(s_server_url), "http://%s/api/upload?id=%s", s_server_ip, s_client_id);
                ESP_LOGI(TAG, "Server discovered at %s", s_server_ip);
                close(sock);
                return ESP_OK;
            }
        }
    }

    close(sock);
    ESP_LOGW(TAG, "Discovery timeout or error");
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_sync_connect_to_server(const char *ssid1, const char *pass1,
                                          const char *ssid2, const char *pass2) {
    if (ssid1 && ssid1[0]) {
        ESP_LOGI(TAG, "Connecting to WiFi: %s%s%s",
                 ssid1,
                 (ssid2 && ssid2[0]) ? " (alt: " : "",
                 (ssid2 && ssid2[0]) ? ssid2 : "");
        if (ssid2 && ssid2[0]) {
            ESP_LOGI(TAG, "Alt SSID: %s", ssid2);
        }
    }

    s_ip_resolved = false;

    esp_err_t ret = wifi_manager_init_sta_dual(ssid1, pass1, ssid2, pass2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    int retry = 0;
    while (!wifi_manager_is_connected() && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (!wifi_manager_is_connected()) {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "WiFi connected, discovering server...");
    vTaskDelay(pdMS_TO_TICKS(500));
    ret = wifi_sync_discover_server(CONFIG_CLIENT_DISCOVER_TIMEOUT_MS);
    if (ret != ESP_OK) {
#ifdef SERVER_IP
        ESP_LOGW(TAG, "Server discovery failed, using fallback IP: %s", SERVER_IP);
        s_ip_resolved = true;
#else
        ESP_LOGE(TAG, "Server discovery failed");
        return ESP_ERR_NOT_FOUND;
#endif
    }

    ESP_LOGI(TAG, "Server URL: %s", s_server_url);
    return ESP_OK;
}

static esp_err_t http_read_response(esp_http_client_handle_t client, char **out_buf, int *out_len) {
    int content_len = esp_http_client_get_content_length(client);
    if (content_len <= 0) content_len = 256;
    if (content_len > 2048) content_len = 2048;
    char *buf = malloc(content_len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    int total = 0;
    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buf + total, content_len - total)) > 0) {
        total += read_len;
        if (total >= content_len) break;
    }
    buf[total] = '\0';
    *out_buf = buf;
    *out_len = total;
    return ESP_OK;
}

static void apply_server_config(cJSON *root) {
    cJSON *ts = cJSON_GetObjectItem(root, "timestamp");
    if (ts && cJSON_IsNumber(ts)) {
        uint32_t timestamp = (uint32_t)ts->valuedouble;
        struct timeval tv;
        tv.tv_sec = (time_t)timestamp;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "System time set from server: %lu", (unsigned long)timestamp);
    }

    cJSON *interval = cJSON_GetObjectItem(root, "sync_interval");
    if (interval && cJSON_IsNumber(interval)) {
        uint32_t val = (uint32_t)interval->valuedouble;
        if (val >= 60) {
            s_sync_interval_sec = val;
            ESP_LOGI(TAG, "Sync interval updated: %lu sec", (unsigned long)val);
        }
    }

    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name)) {
        const char *val = name->valuestring;
        if (val && val[0]) {
            strncpy(s_assigned_name, val, sizeof(s_assigned_name) - 1);
            s_assigned_name[sizeof(s_assigned_name) - 1] = '\0';
            save_assigned_name_to_nvs(s_assigned_name);
            ESP_LOGI(TAG, "Assigned name from server: %s", s_assigned_name);
        }
    }

    cJSON *tz = cJSON_GetObjectItem(root, "timezone");
    if (tz && cJSON_IsString(tz)) {
        const char *val = tz->valuestring;
        if (val && val[0]) {
            setenv("TZ", val, 1);
            tzset();
            ESP_LOGI(TAG, "Timezone from server: %s", val);
        }
    }

    cJSON *bm = cJSON_GetObjectItem(root, "basic_mode");
    if (bm && cJSON_IsBool(bm)) {
        bool new_val = cJSON_IsTrue(bm);
        if (new_val != s_basic_mode) {
            s_basic_mode = new_val;
            save_basic_mode_to_nvs(s_basic_mode);
            ESP_LOGI(TAG, "Basic mode from server: %s", s_basic_mode ? "true" : "false");
        }
    }
}

esp_err_t wifi_sync_get_server_time(uint32_t *timestamp) {
    if (!s_ip_resolved || !wifi_manager_is_connected()) return ESP_ERR_INVALID_STATE;

    char url[128];
    snprintf(url, sizeof(url), "http://%s/api/time", s_server_ip);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK && esp_http_client_get_status_code(client) == 200) {
        char *buf = NULL;
        int len = 0;
        if (http_read_response(client, &buf, &len) == ESP_OK && buf) {
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *ts = cJSON_GetObjectItem(root, "timestamp");
                if (ts && cJSON_IsNumber(ts)) {
                    *timestamp = (uint32_t)ts->valuedouble;
                    ESP_LOGI(TAG, "Server time fetched: %lu", (unsigned long)*timestamp);
                } else {
                    ESP_LOGW(TAG, "Server time response missing 'timestamp'");
                }
                cJSON_Delete(root);
            }
            free(buf);
        }
    } else {
        ESP_LOGW(TAG, "Server time fetch failed: ret=%d", ret);
    }
    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t wifi_sync_upload_unsynced(void) {
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ip_resolved) {
        ESP_LOGW(TAG, "Server IP not resolved");
        return ESP_ERR_INVALID_STATE;
    }

    client_record_t records[UPLOAD_BATCH_SIZE];
    int count = data_storage_read_unsynced(records, UPLOAD_BATCH_SIZE);

    if (count == 0) {
        ESP_LOGI(TAG, "No unsynced records");
        /* Still query server config (time/name/interval) even with no data */
        count = 0;
    }

    ESP_LOGI(TAG, "Uploading %d records", count);

    static char json_buf[4096];
    int offset = 0;
    offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, "{\"readings\":[");

    for (int i = 0; i < count; i++) {
        if (i > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
        int t_int = (int)records[i].temperature;
        int t_dec = (int)(records[i].temperature * 10) % 10;
        if (t_dec < 0) t_dec = -t_dec;
        if (s_basic_mode) {
            offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                "{\"ts\":%ld,\"up\":%u,\"t\":%d.%d,\"h\":%d}",
                (long)records[i].timestamp, records[i].uptime_sec,
                t_int, t_dec, (int)records[i].humidity);
#if ENS160_ENABLE
        } else {
            offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                "{\"ts\":%ld,\"up\":%u,\"t\":%d.%d,\"h\":%d,\"c\":%u,\"v\":%u,\"a\":%u}",
                (long)records[i].timestamp, records[i].uptime_sec,
                t_int, t_dec, (int)records[i].humidity,
                records[i].eco2, records[i].tvoc, records[i].aqi);
#endif
        }
    }

    offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, "]}");

    esp_http_client_config_t config = {
        .url = s_server_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
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
            char *resp_buf = NULL;
            int resp_len = 0;
            if (http_read_response(client, &resp_buf, &resp_len) == ESP_OK && resp_buf) {
                cJSON *root = cJSON_Parse(resp_buf);
                if (root) {
                    apply_server_config(root);
                    cJSON_Delete(root);
                }
                free(resp_buf);
            }
            if (count > 0) {
                data_storage_mark_synced(count);
            }
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

const char *wifi_sync_get_server_ip(void) {
    return s_ip_resolved ? s_server_ip : NULL;
}

const char *wifi_sync_get_assigned_name(void) {
    return s_assigned_name[0] ? s_assigned_name : NULL;
}

uint32_t wifi_sync_get_sync_interval(void) {
    return s_sync_interval_sec;
}

bool wifi_sync_is_basic_mode(void) {
    return s_basic_mode;
}
