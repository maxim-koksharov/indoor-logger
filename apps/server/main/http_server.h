#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the HTTP server with default config.
 *  Allocates internal structures. Does not register URI handlers yet.
 *  @return 0 on success, -1 if httpd_start failed.
 */
int http_server_init(void);

/** @brief Register all URI handlers and begin serving requests.
 *  Must be called after http_server_init().
 *  Registers 6 endpoints: GET /, /api/health, /api/clients, /api/client,
 *  POST /api/upload, GET /api/data.
 *  @return 0 on success, -1 on handler registration failure.
 */
int http_server_start(void);

/** @brief Load sync interval from NVS (call once at boot). */
void config_load_sync_interval(void);

/** @brief Save sync interval to NVS and update global. */
void config_save_sync_interval(uint32_t value);

/** @brief Get current sync interval in seconds. */
uint32_t config_get_sync_interval(void);

/** @brief Load timezone from NVS (call once at boot). */
void config_load_timezone(void);

/** @brief Save timezone to NVS and apply immediately. */
void config_save_timezone(const char *tz);

/** @brief Get current POSIX timezone string. */
const char *config_get_timezone(void);

#ifdef __cplusplus
}
#endif

#endif
