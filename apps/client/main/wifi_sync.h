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
bool wifi_sync_is_basic_mode(void);
const char *wifi_sync_get_client_id(void);

/** @brief Pop the pending client_id change (if any) pushed by the server.
 *  @param out_id  Output buffer for the new ID.
 *  @param out_sz  Size of @p out_id.
 *  @return true if the ID changed since the last call (and the new ID is
 *          copied to @p out_id), false otherwise.
 */
bool wifi_sync_take_id_change(char *out_id, size_t out_sz);

#ifdef __cplusplus
}
#endif
