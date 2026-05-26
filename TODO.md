# TODO

## Architecture (current)

- **WiFi mode**: Both devices STA-only (connect to home router). No AP mode.
- **Dual SSID**: Primary `Domashniy_Plus`, backup `Domashniy`. 3 consecutive NO_AP_FOUND → switch to backup.
- **Client ID**: Custom string (`test_client`, `living_room` etc.), set via Kconfig.
- **Time**: Server stamps `time(NULL)` on upload receipt. Client sends `uptime_sec` as fallback.
- **Server storage**: Binary packed format (14 bytes/record), ring buffer per-client in SPIFFS (max 10000 records).
- **Client storage**: Binary packed format (~16 bytes/record), ring buffer in SPIFFS (max 5000 records).
- **Web UI**: Zero-SPIFFS — HTML/CSS/JS embedded in `.rodata` via `const char[]`. All SPIFFS used for data.
- **Upload**: JSON `POST /api/upload?id=<id>` with body `{"readings":[...]}`. CO2/TVOC/AQI fields are `#if ENS160_ENABLE` guarded.
- **API style**: ESP-IDF v3.4 HTTP server uses exact URI matching (no wildcards). All endpoints use exact URIs with query parameters (`?id=xxx`).
- **Display**: SSD1306 128×32 OLED, dual-screen (temp+uptime / hum+air quality), 3-second cycle, GLCD 5×7 at 2× scale.

## Done

### Server
- [x] Partition table (16MB, no OTA): nvs + phy + factory + ~14MB SPIFFS
- [x] Data store: ring buffer per-client, 10000 max records, `data_store_read_since()` with binary search
- [x] Client registry: in-memory + NVS persistence, stale detection
- [x] HTTP REST API: health, clients, client detail, upload, data (with `since`/`offset`/`limit`)
- [x] Chunked transfer encoding for large data responses (via `httpd_resp_send_chunk`)
- [x] Web UI: 5 metric charts (temp, hum, eCO2, TVOC, AQI) + time range selector (24h/12h/6h/3h)
- [x] JSON built with `snprintf` + integer arithmetic (no `%f`/`%g` — ESP8266 limitation)
- [x] STA-only WiFi with backup SSID, UDP discovery on port 5000
- [x] SNTP sync when connected
- [x] Dual WiFi auto-switching (3 NO_AP_FOUND → backup, reconnect when primary available)
- [x] Self-test at startup: SPIFFS, NVS, HTTP, WiFi status, heap, time source

### Client
- [x] Partition table (4MB, no OTA): nvs + phy + factory + ~3MB SPIFFS
- [x] Local data storage: ring buffer SPIFFS, 5000 max records, unsynced tracking
- [x] I2C drivers: AHT21 (temp/hum), ENS160 (eCO2/TVOC/AQI) with `ENS160_ENABLE` compile guard
- [x] Sensor reading every 5 minutes (300s), storage + display update
- [x] WiFi sync: upload unsynced records every 5 minutes, 5 records/batch
- [x] Direct server IP (`192.168.2.200`), no UDP broadcast
- [x] Dual WiFi: primary + backup SSID
- [x] OLED display: SSD1306 128×32, dual-screen with 3-second toggle
  - Screen 0: temperature (centered) + uptime HH:MM:SS (centered)
  - Screen 1: humidity + eCO2 / TVOC + AQI (compact 2-line layout)
- [x] WiFi manager: dual-SSID, retry logic (60s NO_AP_FOUND, 3600s AUTH_FAIL, backup switch)
- [x] Self-test at startup: I2C, sensors, SPIFFS, WiFi, display

### Shared
- [x] `wifi_manager`: STA-only, dual SSID, auto-retry, backup switching, idempotent init
- [x] SSD1306 display driver (128×32), GLCD 5×7 + Terminus fonts
- [x] AHT21 + ENS160 I2C drivers with retry
- [x] Known ESP8266 quirk workarounds: no `%f` in printf, no wildcard URIs, no `esp_task_wdt_add`

## In Progress
- *(none)*

## Planned

### Short term
- [ ] Add server-side data aggregation (5-minute averages) for full 24h chart coverage
- [ ] Client naming via web UI
- [ ] Clean up old `main/` directory in root (if exists)

### Future
- [ ] Multiple client types (some with ENS160, some without) via `ENS160_ENABLE` in Kconfig
- [ ] Вisplay timeout control via button (GPIO12)
