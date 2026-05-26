#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_manager";

static bool s_connected = false;
static char s_ip_str[16] = {0};
static char s_ap_ip_str[16] = {0};
static EventGroupHandle_t s_wifi_event_group = NULL;
static uint32_t s_retry_delay_sec = 60;
static int32_t s_last_disconnect_reason = 0;
static bool s_wifi_inited = false;

static char s_current_ssid[32] = {0};
static char s_backup_ssid[32] = {0};
static char s_backup_pass[64] = {0};
static bool s_has_backup = false;
static bool s_on_primary = true;
static int s_consecutive_no_ap = 0;
#define MAX_CONSECUTIVE_NO_AP 3
#define STA_CONNECTED_BIT BIT0
#define STA_FAIL_BIT BIT1

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi STA connected");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                s_connected = false;
                s_last_disconnect_reason = event->reason;
                
                switch (event->reason) {
                    case 201: // WIFI_REASON_NO_AP_FOUND
                        s_retry_delay_sec = 60;
                        s_consecutive_no_ap++;
                        ESP_LOGW(TAG, "WiFi network not found (attempt %d/%d), retry in %u sec",
                                 s_consecutive_no_ap, MAX_CONSECUTIVE_NO_AP, s_retry_delay_sec);
                        
                        if (s_has_backup && s_consecutive_no_ap >= MAX_CONSECUTIVE_NO_AP) {
                            s_on_primary = !s_on_primary;
                            s_consecutive_no_ap = 0;
                            
                            const char *ssid = s_on_primary ? s_current_ssid : s_backup_ssid;
                            const char *pass = s_on_primary ? NULL : s_backup_pass;
                            
                            wifi_config_t wifi_config = {0};
                            strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
                            if (pass) {
                                strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
                            }
                            ESP_LOGI(TAG, "Switching to SSID: %s", ssid);
                            esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
                        }
                        break;
                    case 2:  // WIFI_REASON_AUTH_EXPIRE
                    case 4:  // WIFI_REASON_AUTH_FAIL
                    case 204: // WIFI_REASON_INVALID_PMK
                        s_retry_delay_sec = 3600;
                        s_consecutive_no_ap = 0;
                        ESP_LOGW(TAG, "WiFi authentication failed (wrong password?), retry in %u sec", s_retry_delay_sec);
                        break;
                    default:
                        s_retry_delay_sec = 60;
                        s_consecutive_no_ap = 0;
                        ESP_LOGW(TAG, "WiFi disconnected (reason=%d), retry in %u sec", event->reason, s_retry_delay_sec);
                        break;
                }
                
                if (s_wifi_event_group) {
                    xEventGroupSetBits(s_wifi_event_group, STA_FAIL_BIT);
                }
                
                vTaskDelay(pdMS_TO_TICKS(s_retry_delay_sec * 1000));
                esp_wifi_connect();
                break;
            }
            case WIFI_EVENT_AP_START: {
                ESP_LOGI(TAG, "WiFi AP started");
                tcpip_adapter_ip_info_t ip_info;
                tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
                ip4addr_ntoa_r(&ip_info.ip, s_ap_ip_str, sizeof(s_ap_ip_str));
                ESP_LOGI(TAG, "AP IP: %s", s_ap_ip_str);
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                ip4addr_ntoa_r(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
                ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
                s_connected = true;
                s_consecutive_no_ap = 0;
                if (s_wifi_event_group) {
                    xEventGroupSetBits(s_wifi_event_group, STA_CONNECTED_BIT);
                }
                break;
            }
            default:
                break;
        }
    }
}

esp_err_t wifi_manager_init_sta(const char *ssid, const char *password) {
    if (s_wifi_inited) {
        ESP_LOGI(TAG, "WiFi already initialized, updating credentials for %s", ssid);
        strncpy(s_current_ssid, ssid, sizeof(s_current_ssid) - 1);
        s_on_primary = true;
        s_consecutive_no_ap = 0;
        
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        if (password) {
            strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        }
        
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        esp_wifi_disconnect();
        esp_wifi_connect();
        return ESP_OK;
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    tcpip_adapter_init();

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Configure STA
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    strncpy(s_current_ssid, ssid, sizeof(s_current_ssid) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_inited = true;
    ESP_LOGI(TAG, "WiFi STA initialized, connecting to %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_init_ap(const char *ssid, const char *password) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    tcpip_adapter_init();

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Configure AP
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (password == NULL || strlen(password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_get_ip(char *ip_str, size_t len) {
    if (s_connected && strlen(s_ip_str) > 0) {
        strncpy(ip_str, s_ip_str, len - 1);
        ip_str[len - 1] = '\0';
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

bool wifi_manager_is_connected(void) {
    return s_connected;
}

esp_err_t wifi_manager_init_ap_with_sta_fallback(const char *ap_ssid, const char *ap_pass,
                                                   const char *sta_ssid, const char *sta_pass,
                                                   uint32_t timeout_sec) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char *)ap_config.ap.password, ap_pass, sizeof(ap_config.ap.password) - 1);

    if (sta_ssid == NULL || strlen(sta_ssid) == 0) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "WiFi AP started (no STA): %s", ap_ssid);
        return ESP_OK;
    }

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid) - 1);
    if (sta_pass) {
        strncpy((char *)sta_config.sta.password, sta_pass, sizeof(sta_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi APSTA started, trying STA connect to %s", sta_ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        STA_CONNECTED_BIT | STA_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_sec * 1000));

    if (bits & STA_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA connected to %s", sta_ssid);
    } else {
        ESP_LOGW(TAG, "STA failed, falling back to AP-only");
        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_ip(char *ip_str, size_t len) {
    if (strlen(s_ap_ip_str) > 0) {
        strncpy(ip_str, s_ap_ip_str, len - 1);
        ip_str[len - 1] = '\0';
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_get_mac(char *mac_str, size_t len) {
    if (len < 18) return ESP_ERR_INVALID_ARG;
    
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (ret != ESP_OK) return ret;
    
    snprintf(mac_str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

void wifi_manager_set_backup(const char *ssid, const char *password) {
    if (!ssid || strlen(ssid) == 0) {
        s_has_backup = false;
        return;
    }
    
    strncpy(s_backup_ssid, ssid, sizeof(s_backup_ssid) - 1);
    if (password) {
        strncpy(s_backup_pass, password, sizeof(s_backup_pass) - 1);
    }
    s_has_backup = true;
    s_on_primary = true;
    s_consecutive_no_ap = 0;
    ESP_LOGI(TAG, "Backup WiFi configured: %s", s_backup_ssid);
}

const char *wifi_manager_get_current_ssid(void) {
    return s_current_ssid;
}
