#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/apps/sntp.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_spiffs.h"
#include "tcpip_adapter.h"
#include "wifi_manager.h"
#include "data_store.h"
#include "client_registry.h"
#include "http_server.h"

static const char *TAG = "server";

#define STALE_TIMEOUT_SEC 360
#define MAIN_LOOP_INTERVAL_MS 30000
#define UDP_DISCOVER_PORT 5000
#define DEFAULT_SYNC_INTERVAL_SEC 600
#define DEFAULT_TIMEZONE "WET0WEST,M3.5.0/1,M10.5.0/2"

static uint32_t server_start_tick = 0;
static uint32_t g_sync_interval = DEFAULT_SYNC_INTERVAL_SEC;
static char g_timezone[64] = DEFAULT_TIMEZONE;

uint32_t server_get_timestamp(void) {
    time_t now = time(NULL);
    if (now > 0) return (uint32_t)now;
    return (xTaskGetTickCount() - server_start_tick) * portTICK_PERIOD_MS / 1000;
}

static int read_sta_creds(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) return -1;

    size_t len = ssid_sz;
    err = nvs_get_str(nvs, "sta_ssid", ssid, &len);
    if (err != ESP_OK) { nvs_close(nvs); return -1; }

    len = pass_sz;
    err = nvs_get_str(nvs, "sta_pass", pass, &len);
    nvs_close(nvs);
    return (err == ESP_OK) ? 0 : -1;
}

static const char *get_sta_ssid(void) {
    static char ssid[32] = "";
    static char pass[64] = "";
    if (ssid[0] == '\0') {
        if (read_sta_creds(ssid, sizeof(ssid), pass, sizeof(pass)) != 0) {
            ssid[0] = '\0';
        }
    }
    return ssid[0] ? ssid : NULL;
}

static const char *get_sta_pass(void) {
    static char ssid[32] = "";
    static char pass[64] = "";
    if (ssid[0] == '\0') {
        read_sta_creds(ssid, sizeof(ssid), pass, sizeof(pass));
    }
    return pass;
}

/* Priority: NVS > wifi.env compile definitions > Kconfig defaults */
static const char *get_effective_ssid(void) {
    const char *nvs_ssid = get_sta_ssid();
    if (nvs_ssid && nvs_ssid[0] != '\0') return nvs_ssid;
#ifdef WIFI_SSID
    if (WIFI_SSID[0] != '\0') return WIFI_SSID;
#endif
    return CONFIG_SERVER_STA_SSID;
}

static const char *get_effective_pass(void) {
    const char *nvs_pass = get_sta_pass();
    if (nvs_pass && nvs_pass[0] != '\0') return nvs_pass;
#ifdef WIFI_PASS
    if (WIFI_PASS[0] != '\0') return WIFI_PASS;
#endif
    return CONFIG_SERVER_STA_PASS;
}

/* Secondary SSID/pass. Priority: NVS (wifi:sta_ssid2 / wifi:sta_pass2) >
 * wifi.env (WIFI_SSID_2 / WIFI_PASS_2) > Kconfig (CONFIG_SERVER_BACKUP_*).
 * Returns NULL when no secondary is configured. */
static const char *get_secondary_ssid(void) {
    static char ssid[32] = "";
    static char pass[64] = "";
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = sizeof(ssid);
            if (nvs_get_str(nvs, "sta_ssid2", ssid, &len) == ESP_OK && ssid[0]) {
                len = sizeof(pass);
                nvs_get_str(nvs, "sta_pass2", pass, &len);
            } else {
                ssid[0] = '\0';
            }
            nvs_close(nvs);
        }
    }
    if (ssid[0]) return ssid;
#ifdef WIFI_SSID_2
    if (WIFI_SSID_2[0] != '\0') return WIFI_SSID_2;
#endif
    if (strlen(CONFIG_SERVER_BACKUP_SSID) > 0) return CONFIG_SERVER_BACKUP_SSID;
    return NULL;
}

static const char *get_secondary_pass(void) {
    static char ssid[32] = "";
    static char pass[64] = "";
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = sizeof(ssid);
            if (nvs_get_str(nvs, "sta_ssid2", ssid, &len) == ESP_OK && ssid[0]) {
                len = sizeof(pass);
                nvs_get_str(nvs, "sta_pass2", pass, &len);
            } else {
                ssid[0] = '\0';
            }
            nvs_close(nvs);
        }
    }
    if (ssid[0]) return pass;
#ifdef WIFI_PASS_2
    if (WIFI_PASS_2[0] != '\0') return WIFI_PASS_2;
#endif
    return CONFIG_SERVER_BACKUP_PASS;
}

void __attribute__((unused)) write_sta_creds(const char *ssid, const char *pass) {
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_str(nvs, "sta_ssid", ssid);
    nvs_set_str(nvs, "sta_pass", pass);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "STA credentials saved to NVS");
}

uint32_t config_get_sync_interval(void) {
    return g_sync_interval;
}

void config_load_sync_interval(void) {
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READONLY, &nvs) != ESP_OK) {
        g_sync_interval = DEFAULT_SYNC_INTERVAL_SEC;
        return;
    }
    uint32_t value = DEFAULT_SYNC_INTERVAL_SEC;
    esp_err_t err = nvs_get_u32(nvs, "sync_int", &value);
    nvs_close(nvs);
    if (err == ESP_OK && value > 0) {
        g_sync_interval = value;
    } else {
        g_sync_interval = DEFAULT_SYNC_INTERVAL_SEC;
    }
}

void config_save_sync_interval(uint32_t value) {
    if (value == 0) value = DEFAULT_SYNC_INTERVAL_SEC;
    g_sync_interval = value;
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READWRITE, &nvs) != ESP_OK) return;
    esp_err_t err = nvs_set_u32(nvs, "sync_int", value);
    if (err == ESP_OK) {
        nvs_commit(nvs);
        ESP_LOGI(TAG, "Sync interval saved: %u sec", value);
    }
    nvs_close(nvs);
}

static void apply_timezone(const char *tz) {
    if (!tz || !tz[0]) return;
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone applied: %s", tz);
}

const char *config_get_timezone(void) {
    return g_timezone;
}

void config_load_timezone(void) {
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READONLY, &nvs) != ESP_OK) {
        strncpy(g_timezone, DEFAULT_TIMEZONE, sizeof(g_timezone) - 1);
        g_timezone[sizeof(g_timezone) - 1] = '\0';
        return;
    }
    size_t len = sizeof(g_timezone);
    esp_err_t err = nvs_get_str(nvs, "timezone", g_timezone, &len);
    nvs_close(nvs);
    if (err != ESP_OK || g_timezone[0] == '\0') {
        strncpy(g_timezone, DEFAULT_TIMEZONE, sizeof(g_timezone) - 1);
        g_timezone[sizeof(g_timezone) - 1] = '\0';
    }
}

void config_save_timezone(const char *tz) {
    if (!tz || !tz[0]) tz = DEFAULT_TIMEZONE;
    strncpy(g_timezone, tz, sizeof(g_timezone) - 1);
    g_timezone[sizeof(g_timezone) - 1] = '\0';
    apply_timezone(g_timezone);
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READWRITE, &nvs) != ESP_OK) return;
    esp_err_t err = nvs_set_str(nvs, "timezone", g_timezone);
    if (err == ESP_OK) {
        nvs_commit(nvs);
        ESP_LOGI(TAG, "Timezone saved: %s", g_timezone);
    }
    nvs_close(nvs);
}

static void init_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    apply_timezone(config_get_timezone());
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
        ESP_LOGI(TAG, "SNTP synced (local): %s", asctime(&timeinfo));
    } else {
        ESP_LOGW(TAG, "SNTP sync failed, time may be inaccurate");
    }
}

static void udp_discovery_task(void *pvParameters) {
    char server_ip[16] = {0};
    (void)pvParameters;

    while (1) {
        if (!wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        wifi_manager_get_ip(server_ip, sizeof(server_ip));
        if (strlen(server_ip) == 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGW(TAG, "UDP discovery: socket create failed");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(UDP_DISCOVER_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ESP_LOGW(TAG, "UDP discovery: bind failed");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        ESP_LOGI(TAG, "UDP discovery listening on port %d", UDP_DISCOVER_PORT);

        char buf[64];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        while (wifi_manager_is_connected()) {
            int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                               (struct sockaddr *)&from, &fromlen);
            if (len > 0) {
                buf[len] = '\0';
                if (strncmp(buf, "AIRMON_DISCOVER", 15) == 0) {
                    char response[64];
                    int rlen = snprintf(response, sizeof(response),
                                        "AIRMON_RESPONSE %s\n", server_ip);
                    sendto(sock, response, rlen, 0,
                           (struct sockaddr *)&from, fromlen);
                    ESP_LOGI(TAG, "UDP discovery: replied to %s",
                             inet_ntoa(from.sin_addr));
                }
            }
        }

        close(sock);
        ESP_LOGI(TAG, "UDP discovery: WiFi lost, restarting listener");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Server starting...");

    server_start_tick = xTaskGetTickCount();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    config_load_sync_interval();
    ESP_LOGI(TAG, "Sync interval: %u sec", g_sync_interval);
    config_load_timezone();
    ESP_LOGI(TAG, "Timezone: %s", g_timezone);

    if (data_store_init() != 0) {
        ESP_LOGE(TAG, "Failed to init data store");
        return;
    }
    ESP_LOGI(TAG, "Data store initialized");

    client_registry_init();
    int loaded = client_registry_load();
    ESP_LOGI(TAG, "Client registry loaded %d clients", loaded);

    const char *sta_ssid = get_effective_ssid();
    const char *sta_pass = get_effective_pass();
    const char *sta_ssid2 = get_secondary_ssid();
    const char *sta_pass2 = get_secondary_pass();

    if (sta_ssid && strlen(sta_ssid) > 0) {
        if (sta_ssid2 && sta_ssid2[0]) {
            ESP_LOGI(TAG, "STA dual-mode: '%s' <-> '%s' (RSSI-based selection)",
                     sta_ssid, sta_ssid2);
        } else {
            ESP_LOGI(TAG, "STA credentials found, will try connecting to: %s", sta_ssid);
        }
        ESP_ERROR_CHECK(wifi_manager_init_sta_dual(sta_ssid, sta_pass, sta_ssid2, sta_pass2));
        
        char ip_str[16] = {0};
        int retry = 0;
        while (!wifi_manager_is_connected() && retry < 30) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            retry++;
        }
        
        if (wifi_manager_is_connected()) {
            wifi_manager_get_ip(ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "STA connected, IP: %s", ip_str);
#ifdef SERVER_STATIC_IP
            {
                tcpip_adapter_ip_info_t ip_info;
                memset(&ip_info, 0, sizeof(ip_info));
                ip_info.ip.addr = ipaddr_addr(SERVER_STATIC_IP);
#ifdef SERVER_GATEWAY
                ip_info.gw.addr = ipaddr_addr(SERVER_GATEWAY);
#endif
#ifdef SERVER_NETMASK
                ip_info.netmask.addr = ipaddr_addr(SERVER_NETMASK);
#endif
                if (ip_info.ip.addr != 0) {
                    ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));
                    ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
                    ESP_LOGI(TAG, "Static IP applied: %s", SERVER_STATIC_IP);
                }
            }
#endif
            init_sntp();
        } else {
            ESP_LOGW(TAG, "STA connection timeout, will retry in background");
        }
    } else {
        ESP_LOGW(TAG, "No STA credentials configured (NVS or Kconfig). Server will not connect to WiFi.");
    }

    if (http_server_init() != 0) {
        ESP_LOGE(TAG, "Failed to init HTTP server");
        return;
    }
    if (http_server_start() != 0) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }
    
    char server_ip[16] = {0};
    if (wifi_manager_is_connected()) {
        wifi_manager_get_ip(server_ip, sizeof(server_ip));
        ESP_LOGI(TAG, "HTTP server running at http://%s", server_ip);
    } else {
        ESP_LOGI(TAG, "HTTP server running (WiFi not connected)");
    }

    xTaskCreate(udp_discovery_task, "udp_discovery", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "UDP discovery task created");

    // Self-test summary
    {
        uint32_t spiffs_total = 0, spiffs_used = 0;
        esp_err_t spiffs_ret = esp_spiffs_info("storage", &spiffs_total, &spiffs_used);
        bool sntp_ok = (time(NULL) > 0);
        ESP_LOGI(TAG, "=== SELF-TEST ===");
        ESP_LOGI(TAG, "  SPIFFS: %s (total=%u used=%u)",
                 spiffs_ret == ESP_OK ? "OK" : "FAIL", spiffs_total, spiffs_used);
        ESP_LOGI(TAG, "  NVS: OK");
        ESP_LOGI(TAG, "  Registry: %d clients loaded, %d slots free",
                 loaded, CLIENT_REGISTRY_MAX_CLIENTS - loaded);
        ESP_LOGI(TAG, "  HTTP: OK");
        ESP_LOGI(TAG, "  SNTP: %s", sntp_ok ? "synced" : "not available");
        ESP_LOGI(TAG, "  STA: %s",
                 wifi_manager_is_connected() ? "connected" : "not connected");
        if (sta_ssid && strlen(sta_ssid) > 0) {
            ESP_LOGI(TAG, "  Primary SSID: %s", sta_ssid);
        } else {
            ESP_LOGI(TAG, "  STA: not configured");
        }
        if (sta_ssid2 && sta_ssid2[0]) {
            ESP_LOGI(TAG, "  Secondary SSID: %s", sta_ssid2);
        }
        ESP_LOGI(TAG, "  Active SSID: %s",
                 wifi_manager_is_connected() ? wifi_manager_get_current_ssid() : "(not connected)");
        ESP_LOGI(TAG, "  UDP discovery: port %d", UDP_DISCOVER_PORT);
        ESP_LOGI(TAG, "  Time source: %s", sntp_ok ? "NTP" : "uptime counter");
        if (wifi_manager_is_connected()) {
            ESP_LOGI(TAG, "  IP: %s", server_ip);
        }
        ESP_LOGI(TAG, "  Heap: %d KB free", (int)(esp_get_free_heap_size() / 1024));
        ESP_LOGI(TAG, "=== END SELF-TEST ===");
    }

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
