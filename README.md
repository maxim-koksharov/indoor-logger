# Air Quality Monitor — ESP8266 RTOS SDK

ESP8266-based indoor air quality monitoring system with OLED display, ENS160+AHT21 sensors, WiFi data sync, and a web dashboard.

## System Overview

Two ESP8266 devices communicating over WiFi:

```
┌────────────────────┐       WiFi (AP)        ┌────────────────────┐
│   Server (16MB)    │◄──────────────────────►│   Client (4MB)     │
│                    │   192.168.4.0/24       │                    │
│  WiFi AP+STA       │                        │  OLED Display      │
│  HTTP REST API     │                        │  ENS160 + AHT21    │
│  Web Dashboard     │                        │  Local SPIFFS storage
│  Data Aggregation  │                        │  WiFi sync         │
└────────────────────┘                        └────────────────────┘
```

- **Server** (Wemos D1 Mini, 16MB flash): Creates WiFi AP `AirMon-Server`, provides REST API for data upload/query, embedded web dashboard (zero-SPIFFS, inline HTML/CSS/JS), optional STA connection to home WiFi for NTP sync.
- **Client** (Wemos D1 Mini, 4MB flash): Reads ENS160 (eCO₂, TVOC, AQI) and AHT21 (temperature, humidity) sensors, displays on 128×32 OLED, stores readings locally in SPIFFS ring buffer, syncs to server via HTTP JSON upload.

## Hardware Requirements

- 2× Wemos D1 Mini (ESP8266EX)
- 1× ENS160 + AHT21 air quality sensor module (I2C)
- 1× 0.91" SSD1306 OLED display 128×32 (I2C)
- 1× Tactile button (optional, GPIO12)
- Breadboard and jumper wires

### Wiring

All three I2C devices share a single bus (bus 0):

| Wemos Pin | GPIO | Connection |
|---|---|---|
| D1 | GPIO5 | I2C SCL (OLED + ENS160 + AHT21) |
| D2 | GPIO4 | I2C SDA (OLED + ENS160 + AHT21) |
| D6 | GPIO12 | Tactile button (to GND) |
| 3V3 | — | VCC (all modules) |
| GND | — | GND (all modules) |

| Device | I2C Address |
|---|---|
| SSD1306 OLED | `0x3C` |
| ENS160 | `0x53` |
| AHT21 | `0x38` |

## Project Structure

```
├── components/               # Shared IDF-style components
│   ├── display/              # SSD1306 OLED driver + C wrapper
│   ├── sensors/              # ENS160 + AHT21 I2C drivers
│   ├── fonts/                # Bitmap font library (GLCD 5x7, Terminus)
│   └── wifi/                 # WiFi manager (STA/AP/APSTA)
├── apps/
│   ├── server/               # Server firmware (16MB flash)
│   │   └── main/             # main.c, data_store.c, client_registry.c, http_server.c
│   └── client/               # Client firmware (4MB flash)
│       └── main/             # main.c, button.c, data_storage.c, wifi_sync.c
├── README.md
├── AGENTS.md
└── TODO.md
```

## Build & Flash

### Prerequisites

ESP8266 RTOS SDK v3.4 and toolchain installed at `/esp/`:

```bash
export IDF_PATH=/esp/ESP8266_RTOS_SDK
```

### Build

```bash
# Server
cd apps/server && idf.py build

# Client
cd apps/client && idf.py build
```

### Flash

```bash
# Server (16MB, ttyUSB1)
cd apps/server && idf.py -p /dev/ttyUSB1 flash

# Client (4MB, ttyUSB0)
cd apps/client && idf.py -p /dev/ttyUSB0 flash
```

### Serial Monitor

Use Python (idf.py monitor has termios issues in Docker):

```bash
python3 -c "
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 74880, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1); ser.rts = False; time.sleep(2)
buf = b''; deadline = time.time() + 10
while time.time() < deadline:
    data = ser.read(4096)
    if data: buf += data
    elif len(buf) > 0: break
ser.close(); print(buf.decode('utf-8', errors='replace'))
"
```

## Configuration

### Server STA Mode (connect to home WiFi)

The server can connect to an existing WiFi network alongside its AP mode.
Set credentials (one-time) using NVS:

```c
nvs_set_str("wifi", "sta_ssid", "YourWiFiSSID");
nvs_set_str("wifi", "sta_pass", "YourWiFiPassword");
```

If no STA credentials exist, the server runs in AP-only mode.

### Client ID

Currently hardcoded as `test_client`. Change in `apps/client/main/main.c`:
```c
wifi_sync_init("your_client_id");
```

### ENS160 Enable/Disable

ENS160 takes ~3 minutes for first reading and ~1 hour to stabilize.
Compile without ENS160 support:

```c
#define ENS160_ENABLE 0
#include "ens160.h"
```

## API Endpoints (Server)

| Method | URI | Description |
|---|---|---|
| GET | `/` | Web dashboard (embedded HTML) |
| GET | `/api/health` | Server status, uptime, free heap |
| GET | `/api/clients` | List all registered clients |
| GET | `/api/client?id=<id>` | Client details + last 24 records |
| POST | `/api/upload?id=<id>` | Upload sensor data JSON |
| GET | `/api/data?id=<id>&offset=&limit=` | Query stored records |

### Upload JSON Format

```json
{
  "readings": [
    {
      "ts": 1700000000,
      "up": 1234,
      "t": 25.5,
      "h": 55,
      "c": 400,
      "v": 100,
      "a": 1
    }
  ]
}
```

## Known ESP8266 Limitations

- **`%f` in printf/snprintf**: Not supported without `CONFIG_NEWLIB_STDOUT_FLOATING_POINT` (+28 KB). Use integer arithmetic: `(int)(val * 10) % 10`.
- **`vTaskDelay()` in `app_main()`**: Causes `rst cause: 2`. Use `xTaskCreate()` for main loops.
- **`esp_event_loop_create_default()`**: Only call once. Crashes on second call.
- **HTTP server URI wildcards**: ESP-IDF v3.4 does exact `strncmp` matching only. No `*` wildcard support. Use exact paths with query parameters.

## Components

- `components/display/` — C wrapper for SSD1306 OLED driver (128×32, I2C). Provides `display_*()` API: init, on/off, clear, draw pixel/shapes/scaled text.
- `components/sensors/` — ENS160 (eCO₂, TVOC, AQI) and AHT21 (T, RH) I2C sensor drivers. ENS160 enable/disable via `#define ENS160_ENABLE 0`.
- `components/fonts/` — Bitmap font library (GLCD 5x7, Terminus, Roboto, Bitocra). Font data embedded at compile time.
- `components/wifi/` — WiFi manager supporting STA, AP, and AP+STA fallback modes.

## Data Flow

```
Client                             Server
──────                             ──────
ENS160 + AHT21 → sensor_read()
        │
        ▼
data_storage_append()  ← SPIFFS ring buffer
        │
        ▼ (every 30s)
wifi_sync_upload()  ─── POST /api/upload?id=... ──→  data_store_append()
                                                         │
                                                         ▼
                                                    SPIFFS per-client .dat
                                                         │
                                                    Web UI (GET /)
```

## License

Project code: MIT. Third-party components have their own licenses (MIT for SSD1306 driver, OFL for fonts).
