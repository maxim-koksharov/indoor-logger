#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize WiFi in Station (STA) mode and connect to an access point.
 *
 * @param ssid WiFi network SSID
 * @param password WiFi network password
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_manager_init_sta(const char *ssid, const char *password);

/**
 * Initialize WiFi in Access Point (AP) mode.
 *
 * @param ssid AP network SSID
 * @param password AP network password (min 8 chars, or NULL for open)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_manager_init_ap(const char *ssid, const char *password);

/**
 * Get the IP address of the WiFi interface.
 *
 * @param ip_str Buffer to store IP address string (min 16 bytes)
 * @param len Length of the buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_manager_get_ip(char *ip_str, size_t len);

/**
 * Check if WiFi is connected.
 *
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * Initialize WiFi in AP mode with optional STA fallback.
 * If STA credentials are provided, tries to connect in APSTA mode.
 * If STA fails within timeout, falls back to AP-only mode.
 *
 * @param ap_ssid AP network SSID
 * @param ap_pass AP network password (min 8 chars)
 * @param sta_ssid STA network SSID (NULL to skip STA)
 * @param sta_pass STA network password
 * @param timeout_sec STA connection timeout in seconds
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_manager_init_ap_with_sta_fallback(const char *ap_ssid, const char *ap_pass,
                                                   const char *sta_ssid, const char *sta_pass,
                                                   uint32_t timeout_sec);

/**
 * Get the AP interface IP address.
 *
 * @param ip_str Buffer to store IP address string (min 16 bytes)
 * @param len Length of the buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_manager_get_ap_ip(char *ip_str, size_t len);

#ifdef __cplusplus
}
#endif
