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

#ifdef __cplusplus
}
#endif
