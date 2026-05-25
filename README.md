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
│  Web Dashboard     │                        │  Local SPIFFS      │
│  Data Aggregation  │                        │  WiFi sync         │
└────────────────────┘                        └────────────────────┘
```

## Hardware Requirements

- 2× Wemos D1 Mini (ESP8266EX) — 4MB flash для клиента, 16MB для сервера
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

## Build System

Проект использует стандартную ESP-IDF CMake-систему.
**SDK**: ESP8266 RTOS SDK v3.4 (`/esp/ESP8266_RTOS_SDK`)
**Toolchain**: `/esp/bin/xtensa-lx106-elf/bin`

### Основные команды

```bash
export IDF_PATH=/esp/ESP8266_RTOS_SDK

# Menuconfig (настройка параметров)
cd apps/client && idf.py menuconfig   # Client ID, таймаут дисплея
cd apps/server && idf.py menuconfig   # Режим flash, CPU frequency

# Сборка
cd apps/client && idf.py build
cd apps/server && idf.py build

# Прошивка
cd apps/client && idf.py -p /dev/ttyUSB0 flash
cd apps/server && idf.py -p /dev/ttyUSB1 flash

# Очистка
cd apps/client && idf.py fullclean
```

### Что такое `menuconfig` и зачем он нужен?

`idf.py menuconfig` открывает текстовый интерфейс настройки проекта.
Через него можно менять параметры сборки без правки исходников:

**Для клиента** (`Client Configuration`):
- `Client device identifier` — уникальный ID устройства (по умолчанию `test_client`).
  Это значение передаётся как `?id=<client_id>` при upload на сервер.
- `Display auto-off timeout` — через сколько секунд гаснет дисплей (0 = всегда включён).

Изменения сохраняются в `sdkconfig` и применяются при `idf.py build`.
Конфиг-опции доступны в коде как `CONFIG_CLIENT_ID`, `CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC`.

### Serial Monitor

`idf.py monitor` не работает внутри Docker (termios error).
Используйте Python-скрипт:

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

## Self-Test / Проверка работы

При старте оба приложения выводят self-test summary в лог.
Это основной способ проверить, что всё работает:

**Клиент:**
```
=== SELF-TEST ===
  NVS: OK
  I2C: init OK (SDA=GPIO4 SCL=GPIO5)
  Display: initialized
  ENS160: detected
  AHT21: detected
  SPIFFS: OK (total=... used=...)
  Storage: 125 records saved
  Client ID: living_room
  Heap at init: 64 KB
=== END SELF-TEST ===
```

**Сервер:**
```
=== SELF-TEST ===
  SPIFFS: OK (total=... used=...)
  NVS: OK
  Registry: 0 clients loaded, 16 slots free
  HTTP: OK
  SNTP: not available (AP-only)
  STA: not configured (AP-only)
  Time source: uptime counter
  AP IP: 192.168.4.1
  Heap: 102 KB free
=== END SELF-TEST ===
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
    {"ts": 1700000000, "up": 1234, "t": 25.5, "h": 55, "c": 400, "v": 100, "a": 1}
  ]
}
```

### Data Storage Format (SPIFFS)

Each client's data is stored in `/spiffs/<client_id>.dat` as a binary ring buffer:

- **Header** (36 bytes): magic, version, client_id, max_records, write_idx, count
- **Records** (14 bytes each): timestamp, temp*100, hum*100, eco2, tvoc, aqi

## Configuration Details

### Client ID

Настраивается через `menuconfig` → `Client Configuration → Client device identifier`.
Используется в `POST /api/upload?id=<client_id>` и как имя файла данных на сервере.

### Server STA Mode

Сервер может подключаться к домашней WiFi для NTP-синхронизации.
Креденшиалы — в NVS (`wifi:sta_ssid`, `wifi:sta_pass`).
Если не заданы — сервер работает в AP-only режиме без NTP (таймстемпы = uptime).

### ENS160

ENS160 требует ~3 минут прогрева для первого чтения, ~1 час стабилизации.
Для отключения ENS160 на этапе компиляции:

```c
#define ENS160_ENABLE 0
#include "ens160.h"
```

## Known ESP8266 Limitations

- **`%f` in printf/snprintf**: Not supported without `CONFIG_NEWLIB_STDOUT_FLOATING_POINT` (+28 KB). Use integer arithmetic: `(int)(val * 10) % 10`.
- **`vTaskDelay()` in `app_main()`**: Causes `rst cause: 2`. Use `xTaskCreate()` for main loops.
- **`esp_event_loop_create_default()`**: Only call once. Crashes on second call.
- **`esp_task_wdt_add()`**: Not available in v3.4. Use `esp_task_wdt_reset()` in each loop.
- **`httpd_resp_send_err()`**: Not available in v3.4. Use `httpd_resp_send_404()`/`httpd_resp_send_500()` or manually set type+send.
- **HTTP server URI wildcards**: ESP-IDF v3.4 does exact `strncmp` matching only. No `*` wildcard support. Use exact paths with query parameters.

## Components

| Component | Description |
|---|---|
| `components/display/` | C wrapper for SSD1306 OLED driver (128×32, I2C) |
| `components/sensors/` | ENS160 (eCO₂, TVOC, AQI) + AHT21 (T, RH) I2C drivers |
| `components/fonts/` | Bitmap font library (GLCD 5x7, Terminus, Roboto, Bitocra) |
| `components/wifi/` | WiFi manager for STA, AP, and AP+STA fallback |

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
wifi_sync_upload()  ─── POST /api/upload?id=<id> ──→  data_store_append()
                                                          │
                                                          ▼
                                                     SPIFFS per-client .dat
                                                          │
                                                     Web UI (GET /)
```

## Project Structure

```
├── components/               # Shared IDF-style components
│   ├── display/              # SSD1306 OLED driver + C wrapper
│   ├── sensors/              # ENS160 + AHT21 I2C drivers
│   ├── fonts/                # Bitmap font library
│   └── wifi/                 # WiFi manager
├── apps/
│   ├── server/               # Server firmware (16MB flash)
│   │   └── main/             # main.c, data_store.c, client_registry.c, http_server.c
│   └── client/               # Client firmware (4MB flash)
│       └── main/             # main.c, data_storage.c, wifi_sync.c, button.c
│           └── Kconfig.projbuild  # Client ID, display timeout options
├── README.md
├── AGENTS.md
└── TODO.md
```

## License

Project code: MIT. Third-party components have their own licenses.
