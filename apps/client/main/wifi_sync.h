#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_sync_init(const char *client_id);
esp_err_t wifi_sync_discover_server(uint32_t timeout_ms);
esp_err_t wifi_sync_connect_to_server(const char *ssid1, const char *pass1,
                                          const char *ssid2, const char *pass2);
esp_err_t wifi_sync_upload_unsynced(void);
esp_err_t wifi_sync_get_server_time(uint32_t *timestamp);
void wifi_sync_disconnect(void);
bool wifi_sync_is_connected(void);

const char *wifi_sync_get_server_ip(void);
const char *wifi_sync_get_assigned_name(void);
uint32_t wifi_sync_get_sync_interval(void);

#ifdef __cplusplus
}
#endif
