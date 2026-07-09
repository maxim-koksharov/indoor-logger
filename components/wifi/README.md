# WiFi Manager

ESP8266 WiFi connection helper supporting Station (STA), Access Point (AP), and AP+STA fallback modes.

## Files

| File | Description |
|---|---|
| `wifi_manager.c` / `include/wifi_manager.h` | WiFi mode management |

## Dependencies

- `esp_wifi` (ESP-IDF)
- `esp_event` (ESP-IDF)
- `tcpip_adapter` (ESP-IDF)
- `nvs_flash` (ESP-IDF)

## Modes

| Function | Mode | Description |
|---|---|---|
| `wifi_manager_init_sta()` | STA | Connect to an existing WiFi network |
| `wifi_manager_init_ap()` | AP | Create a WiFi access point |
| `wifi_manager_init_ap_with_sta_fallback()` | APSTA → AP | Try STA first; if it fails, fall back to AP-only |

## Usage

```c
#include "wifi_manager.h"

// AP only
wifi_manager_init_ap("MyAP", "password");

// STA only
wifi_manager_init_sta("HomeWiFi", "password");

// AP with STA fallback (tries STA for 30s, falls back to AP)
wifi_manager_init_ap_with_sta_fallback(
    "AirMon-Server", "ap_password",
    "HomeWiFi", "home_password",
    30);

// Check connection
bool connected = wifi_manager_is_connected();

// Get IPs
char ip[16];
wifi_manager_get_ip(ip, sizeof(ip));       // STA IP
wifi_manager_get_ap_ip(ip, sizeof(ip));    // AP IP
```

## Important Notes

- `esp_event_loop_create_default()` is called internally by each init function. **Calling init twice crashes** (`ESP_ERR_INVALID_STATE` → abort). Use `wifi_manager_init_ap_with_sta_fallback()` instead of calling multiple init functions.
- STA auto-reconnects on disconnect (event handler calls `esp_wifi_connect()`).
- AP is always `AirMon-Server` on channel 1, max 4 connections.
