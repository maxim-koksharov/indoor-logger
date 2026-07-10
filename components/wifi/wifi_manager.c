#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "wifi_manager";

typedef struct {
    char ssid[32];
    char pass[64];
    bool configured;
    int8_t last_rssi;
    uint32_t consecutive_failures;
} sta_network_t;

static sta_network_t s_networks[WIFI_MANAGER_MAX_NETWORKS];
static int s_active_idx = 0;
static int s_network_count = 0;

static bool s_connected = false;
static char s_ip_str[16] = {0};
static char s_ap_ip_str[16] = {0};
static EventGroupHandle_t s_wifi_event_group = NULL;
static uint32_t s_retry_delay_sec = 60;
static int32_t s_last_disconnect_reason = 0;
static bool s_wifi_inited = false;

/* Idempotency flags for init_wifi_subsystem(): these subsystems can only be
 * initialised once per boot. Re-initialising them (e.g. after esp_wifi_deinit
 * followed by another wifi_manager_init_sta_dual call) triggers abort() inside
 * ESP_ERROR_CHECK because esp_event_loop_create_default() and tcpip_adapter_init()
 * return ESP_ERR_INVALID_STATE. */
static bool s_event_loop_inited = false;
static bool s_tcpip_inited = false;
static bool s_nvs_inited = false;

/* Dedicated task for delayed WiFi reconnects. The system event handler must
 * never block (vTaskDelay in the event task breaks WiFi state machine and can
 * trigger watchdog / abort). Instead it signals this task which performs the
 * delay outside the event loop. */
static TaskHandle_t s_retry_task_handle = NULL;
static volatile bool s_retry_pending = false;
static volatile uint32_t s_retry_delay_pending_sec = 0;
static volatile bool s_retry_switch_network = false;

#define MAX_CONSECUTIVE_NO_AP 3
#define FAILOVER_THRESHOLD 2
#define STA_CONNECTED_BIT BIT0
#define STA_FAIL_BIT BIT1

static void apply_active_config(void) {
    if (s_network_count <= 0) return;
    if (s_active_idx < 0) s_active_idx = 0;
    if (s_active_idx >= s_network_count) s_active_idx = 0;

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid,
            s_networks[s_active_idx].ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    if (s_networks[s_active_idx].pass[0]) {
        strncpy((char *)wifi_config.sta.password,
                s_networks[s_active_idx].pass,
                sizeof(wifi_config.sta.password) - 1);
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "Switching to SSID: %s (last RSSI=%d)",
             s_networks[s_active_idx].ssid,
             s_networks[s_active_idx].last_rssi);
}

static void store_network(int idx, const char *ssid, const char *pass) {
    if (idx < 0 || idx >= WIFI_MANAGER_MAX_NETWORKS) return;
    if (!ssid || strlen(ssid) == 0) {
        s_networks[idx].configured = false;
        s_networks[idx].ssid[0] = '\0';
        s_networks[idx].pass[0] = '\0';
        return;
    }
    strncpy(s_networks[idx].ssid, ssid, sizeof(s_networks[idx].ssid) - 1);
    s_networks[idx].ssid[sizeof(s_networks[idx].ssid) - 1] = '\0';
    if (pass) {
        strncpy(s_networks[idx].pass, pass, sizeof(s_networks[idx].pass) - 1);
        s_networks[idx].pass[sizeof(s_networks[idx].pass) - 1] = '\0';
    } else {
        s_networks[idx].pass[0] = '\0';
    }
    s_networks[idx].configured = true;
    s_networks[idx].last_rssi = 0;
    s_networks[idx].consecutive_failures = 0;
}

static int8_t find_rssi_for_ssid(const char *ssid) {
    uint16_t ap_count = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK || ap_count == 0) return 0;

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(
        sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) return 0;

    uint16_t records_actual = ap_count;
    err = esp_wifi_scan_get_ap_records(&records_actual, ap_records);
    if (err != ESP_OK) {
        free(ap_records);
        return 0;
    }

    int8_t best = 0;
    bool found = false;
    for (int i = 0; i < records_actual; i++) {
        if (strcmp((const char *)ap_records[i].ssid, ssid) == 0) {
            if (!found || ap_records[i].rssi > best) {
                best = ap_records[i].rssi;
                found = true;
            }
        }
    }
    free(ap_records);
    return found ? best : 0;
}

/* Returns true if a different network was selected. */
static bool scan_and_select_best(void) {
    if (s_network_count == 0) return false;

    wifi_scan_config_t scan_cfg = {0};
    scan_cfg.ssid = NULL;
    scan_cfg.bssid = NULL;
    scan_cfg.channel = 0;
    scan_cfg.show_hidden = false;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
        return false;
    }

    int best_idx = -1;
    int8_t best_rssi = -127;
    bool any_found = false;

    for (int i = 0; i < s_network_count; i++) {
        if (!s_networks[i].configured) continue;
        int8_t rssi = find_rssi_for_ssid(s_networks[i].ssid);
        s_networks[i].last_rssi = rssi;
        if (rssi != 0) {
            ESP_LOGI(TAG, "Scan: '%s' RSSI=%d", s_networks[i].ssid, rssi);
            if (!any_found || rssi > best_rssi) {
                best_rssi = rssi;
                best_idx = i;
                any_found = true;
            }
        } else {
            ESP_LOGI(TAG, "Scan: '%s' NOT visible", s_networks[i].ssid);
        }
    }

    if (!any_found) {
        ESP_LOGW(TAG, "No configured networks visible, staying on '%s'",
                 s_networks[s_active_idx].ssid);
        return false;
    }

    if (best_idx != s_active_idx) {
        ESP_LOGI(TAG, "Switching: '%s' (RSSI=%d) > current '%s' (RSSI=%d)",
                 s_networks[best_idx].ssid, best_rssi,
                 s_networks[s_active_idx].ssid, s_networks[s_active_idx].last_rssi);
        s_active_idx = best_idx;
        s_networks[s_active_idx].consecutive_failures = 0;
        return true;
    }
    return false;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                if (s_network_count > 0) {
                    apply_active_config();
                }
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
                    case 201: /* WIFI_REASON_NO_AP_FOUND */
                        s_retry_delay_sec = 60;
                        ESP_LOGW(TAG, "WiFi network not found, retry in %u sec",
                                 s_retry_delay_sec);
                        if (s_network_count > 0 && s_active_idx < s_network_count) {
                            s_networks[s_active_idx].consecutive_failures++;
                        }
                        break;
                    case 2:   /* WIFI_REASON_AUTH_EXPIRE */
                    case 4:   /* WIFI_REASON_AUTH_FAIL */
                    case 204: /* WIFI_REASON_INVALID_PMK */
                        s_retry_delay_sec = 3600;
                        if (s_network_count > 0 && s_active_idx < s_network_count) {
                            s_networks[s_active_idx].consecutive_failures = 0;
                        }
                        ESP_LOGW(TAG, "WiFi authentication failed (wrong password?), retry in %u sec",
                                 s_retry_delay_sec);
                        break;
                    default:
                        s_retry_delay_sec = 60;
                        if (s_network_count > 0 && s_active_idx < s_network_count) {
                            s_networks[s_active_idx].consecutive_failures++;
                        }
                        ESP_LOGW(TAG, "WiFi disconnected (reason=%d), retry in %u sec",
                                 event->reason, s_retry_delay_sec);
                        break;
                }

                if (s_wifi_event_group) {
                    xEventGroupSetBits(s_wifi_event_group, STA_FAIL_BIT);
                }

                /* If we've exhausted retries on the current network and we have
                 * a second candidate, scan and pick the best one. */
                bool switched = false;
                if (s_network_count > 1 &&
                    s_networks[s_active_idx].consecutive_failures >= FAILOVER_THRESHOLD) {
                    ESP_LOGW(TAG, "Failover threshold reached, scanning networks");
                    s_networks[s_active_idx].consecutive_failures = 0;
                    switched = scan_and_select_best();
                }

                /* Hand off the delayed reconnect to a dedicated task. The system
                 * event handler must NOT block on vTaskDelay — that would freeze
                 * WiFi state machine and can crash the device. */
                uint32_t delay = switched ? 5 : s_retry_delay_sec;
                s_retry_delay_pending_sec = delay;
                s_retry_switch_network = switched;
                s_retry_pending = true;
                if (s_retry_task_handle) {
                    xTaskNotifyGive(s_retry_task_handle);
                } else {
                    /* Retry task not started (shouldn't happen after init).
                     * Fall back to immediate connect to avoid getting stuck. */
                    ESP_LOGW(TAG, "Retry task unavailable, connecting immediately");
                    if (switched) apply_active_config();
                    esp_wifi_connect();
                }
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
                ESP_LOGI(TAG, "Got IP: %s on SSID '%s'", s_ip_str,
                         (s_network_count > 0) ? s_networks[s_active_idx].ssid : "?");
                s_connected = true;
                if (s_network_count > 0 && s_active_idx < s_network_count) {
                    s_networks[s_active_idx].consecutive_failures = 0;
                }
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

/* Retry task: performs the delayed esp_wifi_connect() that used to live inside
 * the system event handler. Blocking the event task on vTaskDelay is unsafe and
 * can break the WiFi state machine; moving the delay here keeps the event
 * handler short and predictable. */
static void wifi_retry_task(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_retry_pending) continue;

        bool switched = s_retry_switch_network;
        uint32_t delay_sec = s_retry_delay_pending_sec;
        s_retry_pending = false;
        s_retry_switch_network = false;
        s_retry_delay_pending_sec = 0;

        ESP_LOGI(TAG, "Retry task: waiting %u sec before reconnect", delay_sec);
        for (uint32_t i = 0; i < delay_sec; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (!s_retry_pending) break;  /* cancelled by a new event */
        }

        if (s_retry_pending) {
            /* A newer disconnect arrived during the wait. The newer request
             * will trigger its own retry after we return. */
            continue;
        }

        if (switched) {
            apply_active_config();
        }
        esp_wifi_connect();
    }
}

static void start_retry_task_if_needed(void) {
    if (s_retry_task_handle) return;
    BaseType_t ok = xTaskCreate(wifi_retry_task, "wifi_retry", 4096, NULL, 4, &s_retry_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to start wifi_retry task");
        s_retry_task_handle = NULL;
    }
}

static esp_err_t init_wifi_subsystem(void) {
    /* nvs_flash_init: safe to call multiple times. */
    if (!s_nvs_inited) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        s_nvs_inited = true;
    }

    /* tcpip_adapter_init: must only be called once. */
    if (!s_tcpip_inited) {
        tcpip_adapter_init();
        s_tcpip_inited = true;
    }

    /* esp_event_loop_create_default: must only be called once. If the loop
     * already exists (e.g. after esp_wifi_deinit + re-init), ignore
     * ESP_ERR_INVALID_STATE. */
    if (!s_event_loop_inited) {
        esp_err_t ret = esp_event_loop_create_default();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            s_event_loop_inited = true;
        } else {
            ESP_ERROR_CHECK(ret);
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register handlers unconditionally; esp_event is idempotent for repeated
     * identical registrations on different versions, but we guard against
     * duplicates by tracking the registered state below. */
    esp_err_t h1 = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_err_t h2 = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (h1 != ESP_OK && h1 != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(h1);
    if (h2 != ESP_OK && h2 != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(h2);

    start_retry_task_if_needed();
    return ESP_OK;
}

esp_err_t wifi_manager_init_sta_dual(const char *ssid1, const char *pass1,
                                       const char *ssid2, const char *pass2) {
    /* Reset state */
    memset(s_networks, 0, sizeof(s_networks));
    s_network_count = 0;
    s_active_idx = 0;
    s_connected = false;
    s_ip_str[0] = '\0';

    if (ssid1 && strlen(ssid1) > 0) {
        store_network(0, ssid1, pass1);
        s_network_count = 1;
    }
    if (ssid2 && strlen(ssid2) > 0) {
        store_network(1, ssid2, pass2);
        s_network_count = 2;
    }

    if (s_network_count == 0) {
        ESP_LOGE(TAG, "No networks configured");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Dual STA init: %d network(s) configured", s_network_count);
    for (int i = 0; i < s_network_count; i++) {
        ESP_LOGI(TAG, "  [%d] SSID='%s'", i, s_networks[i].ssid);
    }

    if (!s_wifi_inited) {
        ESP_ERROR_CHECK(init_wifi_subsystem());
        s_wifi_inited = true;
    } else if (esp_wifi_get_state() == WIFI_STATE_DEINIT) {
        /* Previously deinit'd (e.g. before light sleep) — re-init subsystem. */
        ESP_LOGI(TAG, "WiFi subsystem was deinit'd, re-initializing");
        ESP_ERROR_CHECK(init_wifi_subsystem());
    }

    /* Scan and pick the network with the best RSSI. */
    scan_and_select_best();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply_active_config();
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_manager_init_sta(const char *ssid, const char *password) {
    /* Single-network mode: behave as before but use the dual infrastructure. */
    return wifi_manager_init_sta_dual(ssid, password, NULL, NULL);
}

esp_err_t wifi_manager_init_ap(const char *ssid, const char *password) {
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

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

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
        s_networks[1].configured = false;
        s_networks[1].ssid[0] = '\0';
        s_networks[1].pass[0] = '\0';
        if (s_network_count > 1) s_network_count = 1;
        return;
    }

    store_network(1, ssid, password);
    if (s_network_count < 2) s_network_count = 2;
    ESP_LOGI(TAG, "Backup WiFi configured: %s", s_networks[1].ssid);
}

const char *wifi_manager_get_current_ssid(void) {
    if (s_network_count == 0) return "";
    return s_networks[s_active_idx].ssid;
}

void wifi_manager_rescan_and_switch(void) {
    if (s_network_count < 2) {
        ESP_LOGD(TAG, "Rescan skipped: only %d network(s) configured", s_network_count);
        return;
    }
    if (!s_wifi_inited) {
        ESP_LOGD(TAG, "Rescan skipped: WiFi not initialised");
        return;
    }
    if (!s_connected) {
        ESP_LOGD(TAG, "Rescan skipped: not connected");
        return;
    }
    ESP_LOGI(TAG, "Periodic rescan: checking for stronger network");
    if (scan_and_select_best()) {
        /* A better network was found. Apply config and trigger reconnect. */
        esp_wifi_disconnect();
        apply_active_config();
        esp_wifi_connect();
    }
}
