# Server Application — 16MB ESP8266

WiFi access point, HTTP REST API, sensor data aggregation, and embedded web dashboard.

## Hardware

| Requirement | Detail |
|---|---|
| Board | Wemos D1 Mini (or any ESP8266 with 16MB flash) |
| Flash | 16 MB (custom partition table) |
| Serial | `/dev/ttyUSB1` |

## Partition Table

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 16 KB |
| phy_init | 0xF000 | 4 KB |
| ota_0 | 0x10000 | 1 MB |
| ota_1 | 0x110000 | 1 MB |
| spiffs | 0x310000 | ~13 MB |

## Feature Summary

| Feature | Details |
|---|---|
| WiFi AP | `AirMon-Server` (192.168.4.1), WPA2 |
| WiFi STA | Optional: connect to home WiFi for NTP sync |
| HTTP API | 6 endpoints: health, clients, client detail, upload, data query, web UI |
| Data Store | Per-client binary ring buffer in SPIFFS (14-byte records, 10K records/client) |
| Client Registry | RAM array (max 16 clients) with NVS persistence |
| Web UI | Zero-SPIFFS, embedded HTML/CSS/JS in `.rodata`, Canvas graphs, dark theme |

## STA Credentials

Set via NVS to enable the server to connect to your home WiFi:

```c
nvs_set_str("wifi", "sta_ssid", "YourWiFi");
nvs_set_str("wifi", "sta_pass", "YourPassword");
```

The server will try STA first (30s timeout). On success: AP+STA mode with NTP sync.
On failure: AP-only mode.

## Build

```bash
cd apps/server
idf.py build
idf.py -p /dev/ttyUSB1 flash
```

## API Endpoints

| Method | URI | Description |
|---|---|---|
| GET | `/` | Web dashboard |
| GET | `/api/health` | JSON: status, uptime, free_heap, clients_online |
| GET | `/api/clients` | JSON array of all registered clients |
| GET | `/api/client?id=<id>` | Client details + last 24 data records |
| POST | `/api/upload?id=<id>` | Upload sensor readings (JSON) |
| GET | `/api/data?id=<id>&offset=N&limit=N` | Query stored records |

### Upload Format

```json
{
  "readings": [
    {"ts":1700000000, "up":1234, "t":25.5, "h":55, "c":400, "v":100, "a":1}
  ]
}
```

Fields: `t`=temperature, `h`=humidity, `c`=eCO₂, `v`=TVOC, `a`=AQI.

## Components Used

- `components/wifi` — WiFi manager (AP+STA fallback)
- `components/display` — Only used for `display_init` (I2C bus setup), no display attached to server
- `components/fonts` — Font definitions (used by display component)
- ESP-IDF: `nvs_flash`, `spiffs`, `esp_http_server`, `cJSON`
