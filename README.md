# Air Quality Monitor — ESP8266 RTOS SDK

ESP8266-based indoor air quality monitoring system with OLED display, ENS160+AHT21 sensors, WiFi data sync, and a web dashboard.

## System Overview

Two ESP8266 devices communicating over WiFi (both connect to your home router):

```
┌────────────────────┐       WiFi (STA)        ┌────────────────────┐
│   Server (16MB)    │◄──────────────────────►│   Client (4MB)     │
│                    │   Your Home Router      │                    │
│  STA mode          │       192.168.2.x       │  STA mode          │
│  HTTP REST API     │                         │  OLED Display      │
│  Web Dashboard     │                         │  ENS160 + AHT21    │
│  Data Aggregation  │                         │  Local SPIFFS      │
│  UDP discovery     │                         │  WiFi sync         │
│  Backup WiFi       │                         │  UDP discovery     │
└────────────────────┘                         └────────────────────┘
```

**WiFi Mode:**
- **Server**: STA-only (connects to your router). Supports primary + backup SSID.
- **Client**: STA-only (connects to the same router). Discovers server via UDP broadcast.

**Server Discovery:** Client sends UDP broadcast to port 5000. Server responds with its IP. No static IP needed.

## Hardware Requirements

- 2× Wemos D1 Mini (ESP8266EX) — 4MB flash for the client, 16MB for the server
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
| D5 | GPIO14 | Output LOW (button GND) |
| D6 | GPIO12 | Input pull-up (button sense) |
| 3V3 | — | VCC (all modules) |
| GND | — | GND (all modules) |

| Device | I2C Address |
|---|---|
| SSD1306 OLED | `0x3C` |
| ENS160 | `0x53` |
| AHT21 | `0x38` |

## Build System

The project uses the standard ESP-IDF CMake build system.
**SDK**: ESP8266 RTOS SDK v3.4 (`/esp/ESP8266_RTOS_SDK`)
**Toolchain**: `/esp/bin/xtensa-lx106-elf/bin`

All commands run inside the Docker container (see `AGENTS.md` for build/run instructions).

### Menuconfig (one-time setup)

```bash
# Client: Client ID, WiFi SSID/pass
idf.py -C apps/client menuconfig

# Server: WiFi SSID/pass (primary + backup)
idf.py -C apps/server menuconfig
```

### Build

```bash
idf.py -C apps/client build
idf.py -C apps/server build
```

### Flash

```bash
idf.py -C apps/client -p /dev/ttyUSB0 flash
idf.py -C apps/server -p /dev/ttyUSB1 flash
```

### Clean

```bash
idf.py -C apps/client fullclean
idf.py -C apps/server fullclean
```

> For the full command list, including monitor and erase, see `AGENTS.md`.

## WiFi Configuration

WiFi SSID and password are set through `menuconfig` when building the firmware.

### Server

```bash
idf.py -C apps/server menuconfig
# Server Configuration → Primary WiFi SSID & Password
#                      → Backup WiFi SSID & Password (optional)
idf.py -C apps/server build
idf.py -C apps/server -p /dev/ttyUSB1 flash
```

**Operating mode:**
- The server connects to your router (STA mode)
- Supports a primary and a backup WiFi network (switches after 3 failed attempts)
- Runs a UDP discovery service on port 5000
- The client finds the server automatically via UDP broadcast
- When the network is unreachable → retries connection every 60 sec
- On wrong password → retries every 60 min

**NVS (alternative to menuconfig):**
Credentials can be stored in NVS (survives reflash):
```c
// Via provisioning or a debug script:
nvs_set_str("wifi", "sta_ssid", "YourNetwork");
nvs_set_str("wifi", "sta_pass", "YourPassword");
nvs_commit();
```
NVS takes precedence over menuconfig.

### Client

```bash
idf.py -C apps/client menuconfig
# Client Configuration → Client device identifier
#                      → WiFi SSID (same as server)
#                      → WiFi password (same as server)
idf.py -C apps/client build
idf.py -C apps/client -p /dev/ttyUSB0 flash
```

**Operating mode:**
- The client connects to the same router (STA mode)
- Sends a UDP broadcast on port 5000 to discover the server
- Automatically uses the IP returned by the server for data uploads

### What is `menuconfig` and why do I need it?

`idf.py menuconfig` opens a text-based project configuration UI.
It lets you change build parameters without editing source files:

**For the client** (`Client Configuration`):
- `Client device identifier` — unique device ID (default `test_client`).
  Used as `?id=<client_id>` when uploading to the server.
- `WiFi SSID / Password` — router credentials (must match the server).

Changes are saved in `sdkconfig` and applied on `idf.py build`.
The config options are exposed in code as `CONFIG_CLIENT_ID`, etc.

**Display:** Off by default. Turns on for 6 seconds on a button press
(2 screens × 3 seconds: temp/time → CO₂/VOC/AQI).

### Serial Monitor

`idf.py monitor` does not work inside Docker (termios error).
Use this Python script instead:

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

## USB Connection & Serial Debug

Both ESP8266 boards are connected to the host via USB and are passed through to the
container by `docker compose.yml`. All build, flash, and serial debug commands
work **from inside the container** — there is no need to switch to the host shell.

### Device Mapping

| Host device | Board | Used for |
|---|---|---|
| `/dev/ttyUSB0` | Client (Wemos D1 Mini + OLED + ENS160 + AHT21) | Flash, monitor, debug |
| `/dev/ttyUSB1` | Server (Wemos D1 Mini, 16MB flash) | Flash, monitor, debug |

The container is configured with:
- `devices:` passing `/dev/ttyUSB0` and `/dev/ttyUSB1` through
- `device_cgroup_rules: c 188:* rmw` to allow `esptool` / `idf.py` to manage them
- `group_add: ["20"]` (the `dialout` group on the host) so the in-container `dev` user
  can read/write serial ports without `sudo`

### Verifying the connection

Open a shell in the container and check that the devices are visible:

```bash
docker compose run --rm bash

# Inside the container:
ls -l /dev/ttyUSB*
# crw-rw---- 1 root dialout 188, 0 ... /dev/ttyUSB0
# crw-rw---- 1 root dialout 188, 1 ... /dev/ttyUSB1

lsusb
# Bus 001 Device 005: ID 1a86:7523 QinHeng Electronics CH340 serial converter
# ...

# Confirm the in-container user is in dialout:
id
# uid=1000(dev) gid=1000(dev) groups=1000(dev),20(dialout)
```

If `/dev/ttyUSB*` are missing, the host likely does not have permission or the
devices are not enumerated yet:

```bash
# On the host:
ls -l /dev/ttyUSB*              # check device presence
sudo usermod -aG dialout $USER  # add your user to dialout (one-time)
# then log out / log in
```

### Tailing serial output (one-shot, 10s)

Capture the boot log without leaving the container:

```bash
docker compose run --rm bash -c '
  python3 -c "
import serial, time
ser = serial.Serial(\"/dev/ttyUSB0\", 74880, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1); ser.rts = False; time.sleep(2)
buf = b\"\"; deadline = time.time() + 10
while time.time() < deadline:
    data = ser.read(4096)
    if data: buf += data
    elif len(buf) > 0: break
ser.close(); print(buf.decode(\"utf-8\", errors=\"replace\"))
"
'
```

For the server, replace `/dev/ttyUSB0` with `/dev/ttyUSB1`.

### Continuous log tail (interactive)

For long-running debug sessions, keep the port open and follow output:

```bash
docker compose run --rm bash

# Inside the container:
stty -F /dev/ttyUSB0 74880 raw -echo
cat /dev/ttyUSB0
# press RST on the board to see fresh boot logs
# Ctrl-C to exit
```

### Flashing a board

Flash commands work the same as on the host — just run them from inside the container:

```bash
docker compose run --rm bash

# Inside the container:
cd /workspace/apps/client
idf.py -p /dev/ttyUSB0 flash

cd /workspace/apps/server
idf.py -p /dev/ttyUSB1 flash
```

### Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Could not open /dev/ttyUSB0` | Device busy or not present | Unplug/replug the USB cable; check `lsusb` on the host |
| `Permission denied` on `/dev/ttyUSB*` | User not in `dialout` | `sudo usermod -aG dialout $USER` on the host, then re-login |
| `esptool: failed to connect` | Wrong port or board not in flash mode | Hold `FLASH` button on Wemos D1 Mini during reset, or check wiring |
| Container cannot see `/dev/ttyUSB*` | Device path differs (e.g. `/dev/ttyACM0`) | Edit `compose.yml` `devices:` list and `device_cgroup_rules` (use `c 166:* rmw` for ACM) |
| Garbled serial output | Wrong baud rate | Wemos D1 Mini bootloader prints at **74880 baud**, runtime at **115200** |

## Self-Test

On startup, both applications log a self-test summary.
This is the primary way to verify that everything works:

**Client:**
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

**Server:**
```
=== SELF-TEST ===
  SPIFFS: OK (total=... used=...)
  NVS: OK
  Registry: 0 clients loaded, 16 slots free
  HTTP: OK
  SNTP: synced
  STA: connected
  STA config: YourNetwork
  Time source: NTP
  IP: 192.168.1.100
  Heap: 102 KB free
=== END SELF-TEST ===
```

## API Endpoints (Server)

Once connected to the router, the server is reachable on the DHCP-assigned IP (or a static one).
Use the IP directly: `http://192.168.1.100/`

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

Configured via `menuconfig` → `Client Configuration → Client device identifier`.
Used in `POST /api/upload?id=<client_id>` and as the data filename on the server.

### Server STA Mode

The server connects to your home WiFi (router) to host the HTTP API.
Credentials are set via `menuconfig` (Server Configuration → SERVER_STA_SSID/PASS,
SERVER_BACKUP_SSID/PASS) or stored in NVS (`wifi:sta_ssid`, `wifi:sta_pass`).

**Retry behavior:**
- On network not found → retry every 60 sec
- On auth failure (wrong password) → retry every 60 min
- On other connection errors → retry every 60 sec
- After 3 consecutive `NO_AP_FOUND` events → switch to backup SSID (if configured)

### ENS160

ENS160 requires ~3 minutes of warm-up for the first reading and ~1 hour to stabilize.
To disable ENS160 at compile time:

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
- **mDNS not available on ESP8266**: mdns.h references `ip6_addr_t` which is not defined. Use UDP broadcast discovery (port 5000) instead.
- **WiFi retry behavior**:
  - Network not found (reason 201) → retry after 60 seconds
  - Wrong password (reason 2, 4, 204) → retry after 3600 seconds (1 hour)
  - Other disconnect reasons → retry after 60 seconds
  - After 3 consecutive NO_AP_FOUND → automatically switch to backup SSID

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
        ▼ (every 30s, via router)
UDP broadcast → port 5000 → discovery
        │
        ▼
wifi_sync_upload()  ─── POST /api/upload?id=<id> ──→  data_store_append()
                                                          │
                                                          ▼
                                                     SPIFFS per-client .dat
                                                          │
                                                     Web UI (GET /)
```

**Discovery Protocol:**
1. Client sends `AIRMON_DISCOVER` to UDP broadcast:5000
2. Server responds with `AIRMON_RESPONSE <ip>`
3. Client caches the IP and uses it for all upload requests
4. On upload error → rediscovery on the next sync cycle

## Project Structure

```
├── components/               # Shared IDF-style components
│   ├── display/              # SSD1306 OLED driver + C wrapper
│   ├── sensors/              # ENS160 + AHT21 I2C drivers
│   ├── fonts/                # Bitmap font library
│   └── wifi/                 # WiFi manager (STA-only with retry logic)
├── apps/
│   ├── server/               # Server firmware (16MB flash)
│   │   └── main/             # main.c, data_store.c, client_registry.c, http_server.c
│   └── client/               # Client firmware (4MB flash)
│       └── main/             # main.c, data_storage.c, wifi_sync.c, button.c
├── README.md
├── AGENTS.md
└── TODO.md
```

## License

Project code: MIT. Third-party components have their own licenses.
