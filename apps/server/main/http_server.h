#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

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

#ifdef __cplusplus
}
#endif

#endif
