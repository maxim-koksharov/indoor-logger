# Client Application — 4MB ESP8266

OLED display, sensor readings, local data storage, and WiFi sync to server.

## Hardware

| Requirement | Detail |
|---|---|
| Board | Wemos D1 Mini (4MB flash) |
| Sensors | ENS160 (I2C, addr 0x53) + AHT21 (I2C, addr 0x38) |
| Display | SSD1306 OLED 128×32 (I2C, addr 0x3C) |
| Button | GPIO12 (optional, tactile switch to GND) |
| Serial | `/dev/ttyUSB0` |

## Partition Table

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 16 KB |
| phy_init | 0xF000 | 4 KB |
| factory | 0x10000 | 1 MB |
| spiffs | 0x110000 | ~3 MB |

## Feature Summary

| Feature | Details |
|---|---|
| Sensors | ENS160 (eCO₂, TVOC, AQI) + AHT21 (temp, humidity) |
| Display | SSD1306 128×32, scaled text, updates every 5s |
| Storage | Local ring buffer in SPIFFS (5000 records max) |
| WiFi Sync | Connects to server AP, uploads JSON every 30s |
| Architecture | FreeRTOS task (`client_task`, 4KB stack) for main loop |

## Display Pages

The display shows two lines updated every 5 seconds:

```
26.5C 52% AQI1       ← temp, humidity, AQI
eCO2 400 TVOC 12     ← ENS160 readings
```

If ENS160 is warming up or disabled:
```
26.5C 52%             ← temp, humidity only
ENS warming...        ← placeholder
```

## ENS160 Configuration

ENS160 requires ~3 minutes for first valid reading. To compile without ENS160:

```c
#define ENS160_ENABLE 0
#include "ens160.h"
```

When disabled, only AHT21 temperature/humidity is reported. Display shows "ENS warming..." and eCO₂/TVOC/AQI values are zero.

## Build

```bash
cd apps/client
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

## Client ID

Default: `test_client`. Change in `main.c`:
```c
wifi_sync_init("your_custom_id");
```

## Data Flow

```
Every 5 seconds:
  ENS160 + AHT21 → read sensors
                → save to SPIFFS (client_record_t)
                → update display

Every 30 seconds:
  Connect to AirMon-Server
  Read 5 unsynced records from SPIFFS
  POST JSON to http://192.168.4.1/api/upload?id=<id>
  Mark records synced on 200 OK
```

## Components Used

- `components/display` — SSD1306 OLED driver + Display wrapper
- `components/sensors` — ENS160 + AHT21 sensor drivers
- `components/fonts` — Bitmap font library (GLCD 5x7)
- `components/wifi` — WiFi manager (STA mode only)
- ESP-IDF: `nvs_flash`, `spiffs`, `esp_http_client`, `driver/i2c`

## Known Limitations

- Client ID is hardcoded (needs Kconfig/NVS integration)
- `time(NULL)` returns 0 (no RTC/NTP); uptime_sec used instead
- Deep sleep not yet implemented (currently always-on for debugging)
- `%f` does not work in ESP8266 printf; all float display uses integer math
