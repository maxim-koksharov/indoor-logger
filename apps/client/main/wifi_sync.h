#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_sync_init(const char *client_id);
esp_err_t wifi_sync_connect_to_server(const char *ap_ssid, const char *ap_pass);
esp_err_t wifi_sync_upload_unsynced(void);
void wifi_sync_disconnect(void);
bool wifi_sync_is_connected(void);

#ifdef __cplusplus
}
#endif
