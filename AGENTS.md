# AGENTS.md

## Project overview

ESP8266 embedded firmware monorepo (ESP8266 RTOS SDK / ESP-IDF style) with:
- **Shared components**: SSD1306 OLED display driver, ENS160+AHT21 sensor drivers, WiFi manager, fonts
- **Server app** (16MB ESP8266): STA WiFi, HTTP REST API, web dashboard, data aggregation
- **Client app** (4MB/16MB ESP8266): OLED display, sensor readings, local storage, WiFi data sync to server

**WiFi Mode:** Both devices connect to your home router (STA-only). No AP mode.

C code only. All FreeRTOS and ESP SDK headers must be wrapped in `extern "C" {}` when included from C++ files.

## Development environment

**All development runs inside a Docker container** based on `ubuntu:20.04` (image: `esp8266-env`).

- Container image built from `Dockerfile` at project root
- `docker compose.yml` defines the runtime: bind-mounts the project, USB devices, and the host's opencode config/license
- Project directory mounted at `/workspace`
- SDK located at `/esp/ESP8266_RTOS_SDK`
- Toolchain at `/esp/bin/xtensa-lx106-elf/bin`
- Two USB serial devices passed through:
  - `/dev/ttyUSB0` — **Client** (Wemos D1 Mini + OLED display + ENS160+AHT21 sensors)
  - `/dev/ttyUSB1` — **Server** (Wemos D1 Mini, 16MB flash)
- Container runs as the host user (UID:GID from `.env`) so files keep host ownership
- `~/.config/opencode` and `~/.local/share/opencode` are bind-mounted, so the opencode **license/auth from the host** is used inside the container

### Run commands (from project root on the host)

| Action | Command |
|---|---|
| Build image (first time, or after Dockerfile change) | `docker compose build` |
| Interactive shell | `docker compose run --rm bash` |
| opencode TUI | `docker compose run --rm opencode` |

## Build system

**CMake only.** Build each application from its own directory.

### Setup

```bash
export IDF_PATH=/esp/ESP8266_RTOS_SDK
```

This is already set in the Docker image via `ENV`.

### Commands

| Action | Command |
|---|---|
| Configure (client) | `cd apps/client && idf.py menuconfig` |
| Configure (server) | `cd apps/server && idf.py menuconfig` |
| Build (client) | `cd apps/client && idf.py build` |
| Build (server) | `cd apps/server && idf.py build` |
| Flash (client) | `cd apps/client && idf.py -p /dev/ttyUSB0 flash` |
| Flash (server) | `cd apps/server && idf.py -p /dev/ttyUSB1 flash` |
| Monitor (client) | `cd apps/client && idf.py -p /dev/ttyUSB0 monitor` |
| Monitor (server) | `cd apps/server && idf.py -p /dev/ttyUSB1 monitor` |
| Erase flash (client) | `cd apps/client && idf.py -p /dev/ttyUSB0 erase_flash` |
| Erase flash (server) | `cd apps/server && idf.py -p /dev/ttyUSB1 erase_flash` |

The `xtensa-lx106-elf-*` toolchain binaries are already in `PATH`.

## Project structure

```
/workspace/
├── components/                 # Shared libraries (IDF-style components)
│   ├── display/                # SSD1306 OLED driver + C Display wrapper
│   ├── sensors/                # ENS160 + AHT21 sensor drivers
│   ├── fonts/                  # Bitmap font library (GLCD, Terminus, etc.)
│   └── wifi/                   # WiFi connection helper (STA-only with retry logic)
├── apps/
│   ├── server/                 # Server application (16MB Flash ESP8266)
│   │   ├── CMakeLists.txt
│   │   ├── main/               # main.c, data_store.c, client_registry.c, http_server.c
│   │   ├── Kconfig.projbuild   # WiFi SSID/pass options
│   │   └── sdkconfig
│   └── client/                 # Client application (4MB ESP8266)
│       ├── CMakeLists.txt
│       ├── main/               # main.c, button.c, data_storage.c, wifi_sync.c
│       ├── Kconfig.projbuild   # Client ID, display timeout, WiFi SSID/pass, server IP
│       └── sdkconfig
├── TODO.md
├── AGENTS.md
└── README.md
```

## Entry point

`app_main()` in `apps/<app>/main/main.c` — **not** `user_init`.

## Client sensor & sync intervals

| Interval | Value | Description |
|---|---|---|
| Sensor read | 5 minutes (300000 ms) | Reads AHT21 + ENS160, stores record to SPIFFS |
| WiFi sync | 5 minutes (300000 ms) | Uploads unsynced records to server |
| Display switch | 3 seconds (3000 ms) | Toggles between screen 0 and screen 1 |
| Sleep cycle | 100 ms - 5 min | `esp_light_sleep_start()` — blocks until timer or GPIO wake |

## Client power saving modes

The client supports the following sleep modes, ordered by power consumption (lowest first):

| Mode | Current | Wake sources | Wake time | WiFi required? | Button wake? | Notes |
|---|---|---|---|---|---|---|
| **Deep sleep** | **~20 µA** | RTC timer only | ~30 ms (full boot) | Must stop first | No (RST mod only) | Lowest power. Full boot from scratch. No GPIO wake-up in ESP-IDF v3.4. |
| **Deep sleep + RST** | **~20 µA** | Timer + RST pin | ~30 ms (full boot) | Must stop first | Yes (button → RST) | Button to RST via transistor. Full reset on press. |
| **Light sleep** | **~0.7–3 mA** | Timer (RTC) + GPIO (level) | ~2.7 ms | Must stop first | Yes (any GPIO) | RAM retained, CPU paused. GPIO must be level-triggered (LOW/HIGH), not edge. |
| **Modem sleep** | **~12–15 mA** | WiFi beacon (DTIM) | ~1–3 ms (RF PLL) | Must be connected | N/A | CPU runs, RF off between beacons. |
| **Active (no sleep)** | **~50 mA** | N/A | N/A | Always on | N/A | RF + CPU always on. **CPU at 80 MHz** (reduced from 160 MHz for power saving). |

### Wake-up times in detail

| Source | Mode | Time | Notes |
|---|---|---|---|
| GPIO interrupt (NEGEDGE) | Active / Modem sleep | **Instant** (<1 ms) | ISR → `vTaskNotifyGiveFromISR` → task resumes |
| GPIO level (LOW/HIGH) | Light sleep | **~2.7 ms** | RTC wake-up, PLL relock, crystal stabilize. **Current mode.** |
| RTC timer | Light sleep | **~2.7 ms** | Same path as GPIO wake-up |
| RTC timer | Deep sleep | **~30–35 ms** | Full ROM boot, flash init, `app_main()` |
| RST pin | Deep sleep | **~30–35 ms** | Same as power-on reset |
| WiFi reconnect | Any (after stop) | **~2–5 s** | DHCP + 4-way handshake; only needed after WiFi stop/start |

### Current architecture (light sleep)

The client uses light sleep mode with WiFi stop/start cycles. This provides ~0.7-3 mA power consumption with both timer and button wake-up support.

```
[Boot] → WiFi connect → sensor read + upload → WiFi stop → light sleep
[Timer wake] → WiFi start → sensor read + upload → WiFi stop → light sleep
[Button wake] → display ON 6s → light sleep (no WiFi)
```

**Key implementation details:**
- `button_enable_wakeup()` configures GPIO12 with `GPIO_INTR_LOW_LEVEL` before each sleep
- `esp_light_sleep_start()` blocks until wake (timer or GPIO)
- After timer wake: `esp_wifi_start()` triggers auto-reconnect via WIFI_EVENT_STA_START
- After button wake: display updates, no WiFi needed, returns to sleep
- RAM preserved during light sleep, so static variables retain values

### Light sleep architecture

For lower power (~0.7-3 mA), use light sleep. WiFi must be stopped before sleep. After wake:
- **Button press**: display on for 6s, no WiFi needed → sleep again
- **Timer (5 min)**: restart WiFi → read sensors → upload → stop WiFi → sleep again

WiFi stop/start takes ~2–5 s per cycle.

### Deep sleep architecture (lowest power, timer-only)

For battery-powered operation without button response:
```c
esp_wifi_stop();
esp_deep_sleep(300000000);  // 5 min
```
On wake: full boot → `app_main()` → WiFi connect → read sensors → upload → deep sleep.

Button cannot wake from deep sleep without hardware modification (button → RST pin).

### Button → RST hardware modification

To enable button wake from deep sleep:
1. Connect button leg 1 → GPIO12 (D6)
2. Connect button leg 2 → RST pin via NPN transistor (collector = RST, emitter = GND, base = GPIO12 via 1kΩ)
3. D5 (GPIO14) = output LOW (no longer needed for button GND)
4. Use `esp_deep_sleep(0)` (no timer) or `esp_deep_sleep(time_us)` (with timer)
5. OR simpler: button directly to RST (but leaves ESP8266 floating risk)

The client must be configured for the chosen power mode (default: light sleep).

## Client OLED display — dual-screen layout (128×32)

Two screens cycle every 3 seconds (display is ON for 6s total). GLCD 5×7 font at 2× scale (12px/char, 14px tall).

### Screen 0 — Temperature + Time
```
       27.5C         (centered)
     01:23:45        (centered, HH:MM:SS time)
```

### Screen 1 — Humidity + Air Quality
```
46%RH 754e          (humidity, eCO2)
252TV AQI3          (TVOC, Air Quality Index)
```

`update_display()` is called on button press (screen 0), then at 3s (switch to screen 1), and on every sensor read (every 5 minutes).

## Agent rules for the codebase and building

If you need to verify your changes, use command "Flash". Flash device only with Flash command.

Add comments to code in English only.

## Known ESP8266 platform quirks

### `%f` does not work in printf/snprintf

ESP8266 newlib does NOT support `%f` / `%lf` format specifiers in `printf`, `snprintf`, `ESP_LOGI` etc. unless `CONFIG_NEWLIB_STDOUT_FLOATING_POINT` is enabled (adds ~28 KB to binary).

**Fix**: Always use integer arithmetic:
```c
int t_int = (int)temperature;
int t_dec = (int)(temperature * 10) % 10;
if (t_dec < 0) t_dec = -t_dec;
snprintf(buf, sz, "%d.%d", t_int, t_dec);
```

### ESP-IDF v3.4 HTTP server does not support wildcard `*` in URIs

The `httpd_register_uri_handler()` function uses `strncmp` for exact string matching only (see `httpd_uri.c` line 164). A URI template like `/api/clients/*/upload` will never match inbound requests.

**Fix**: Use exact URI paths with query parameters:
- `GET /api/client?id=test_client` (not `/api/clients/*`)
- `POST /api/upload?id=test_client` (not `/api/clients/*/upload`)

### `esp_event_loop_create_default()` must be called only once

Calling this a second time returns `ESP_ERR_INVALID_STATE` → `abort()`. Guard against double init when combining `wifi_manager_init_sta()` after `wifi_manager_init_ap_with_sta_fallback()`.

### `vTaskDelay(1000)` crashes in `app_main`'s direct `while(1)`

On ESP8266, calling `vTaskDelay()` directly in `app_main()`'s `while(1)` loop causes `rst cause: 2`. Always create a separate FreeRTOS task with `xTaskCreate()` for main loop logic.

### `esp_task_wdt_add(NULL)` does not exist in ESP-IDF v3.4

ESP-IDF v3.4 for ESP8266 does not have `esp_task_wdt_add()`. Only `esp_task_wdt_init()` and `esp_task_wdt_reset()` / `esp_task_wdt_feed()` are available. Call `esp_task_wdt_reset()` in each loop iteration instead.

### ENS160 upload JSON — conditional fields

When `ENS160_ENABLE=1` (default), upload JSON includes eCO2/TVOC/AQI:
```json
{"readings":[{"ts":123,"up":456,"t":27.0,"h":46,"c":754,"v":252,"a":3}]}
```
When `ENS160_ENABLE=0`, the JSON omits `c`, `v`, `a` fields:
```json
{"readings":[{"ts":123,"up":456,"t":27.0,"h":46}]}
```
The server handles both formats (missing fields default to 0).

### `httpd_resp_send_err()` does not exist in ESP-IDF v3.4 HTTP server

The `httpd_resp_send_err()` function and `HTTPD_400_BAD_REQUEST` / `HTTPD_404_NOT_FOUND` constants are not available. Use `httpd_resp_send_404(req)`, `httpd_resp_send_500(req)`, or manually set type and send:
```c
httpd_resp_set_type(req, "text/plain");
httpd_resp_send(req, "error message", 13);
```

### WiFi retry behavior (STA mode)

The `wifi_manager` implements automatic reconnection with different delays based on disconnect reason:

| Disconnect reason | Code | Retry delay |
|---|---|---|
| Network not found | 201 (`WIFI_REASON_NO_AP_FOUND`) | 60 seconds |
| Wrong password | 2, 4, 204 (`AUTH_EXPIRE`, `AUTH_FAIL`, `INVALID_PMK`) | 3600 seconds (1 hour) |
| Other reasons | various | 60 seconds |

After 3 consecutive `NO_AP_FOUND` events, the manager automatically switches to the backup SSID (if configured). On next connection, it switches back to primary.

### `mdns` component is broken on ESP8266 v3.4

The ESP8266 RTOS SDK v3.4 mDNS component references `ip6_addr_t` which is not defined. Instead, use UDP broadcast discovery (implemented in server and client).

## Server timestamp fallback

When NTP is unavailable, `time(NULL)` returns 0. The server uses `server_get_timestamp()` which returns `time(NULL)` if valid (> 0), otherwise returns uptime since server start. Declare `extern uint32_t server_get_timestamp(void);` in any C file that needs it.

## Kconfig options

### Server (`apps/server/main/Kconfig.projbuild`)

| Option | Default | Description |
|---|---|---|
| `CONFIG_SERVER_STA_SSID` | `""` | Primary WiFi SSID for STA mode |
| `CONFIG_SERVER_STA_PASS` | `""` | Primary WiFi password for STA mode |
| `CONFIG_SERVER_BACKUP_SSID` | `""` | Backup WiFi SSID (switch after 3 consecutive NO_AP_FOUND) |
| `CONFIG_SERVER_BACKUP_PASS` | `""` | Backup WiFi password |

Credentials can also be stored in NVS (`wifi:sta_ssid`, `wifi:sta_pass`). NVS takes precedence over Kconfig.

### Client (`apps/client/main/Kconfig.projbuild`)

| Option | Default | Description |
|---|---|---|
| `CONFIG_CLIENT_ID` | `test_client` | Client identifier for uploads |
| `CONFIG_CLIENT_DISPLAY_TIMEOUT_SEC` | 10 | Display auto-off timeout (0 = always-on) |
| `CONFIG_CLIENT_WIFI_SSID` | `""` | WiFi SSID (same as server) |
| `CONFIG_CLIENT_WIFI_PASS` | `""` | WiFi password (same as server) |
| `CONFIG_CLIENT_DISCOVER_TIMEOUT_MS` | 3000 | UDP broadcast discovery timeout |

## Self-test at startup

Both apps log a self-test summary at startup after all components are initialized. The output includes SPIFFS status, sensor detection, NVS, heap, and connection mode. This is the primary way to verify correct operation on real hardware.

## ENS160 Sensor Notes

| Feature | Detail |
|---|---|
| I2C address | `0x53` |
| Initial warm-up | ~3 minutes for first valid reading after power-on |
| Stabilization | ~1 hour for accurate readings; 48-hour burn-in for best accuracy |
| Operating mode | Must be set to `0x02` (STANDARD), NOT `0x01` (IDLE) |
| Status flag | Bit 1 of status register (0x20) indicates new data ready |

### ENS160_ENABLE compile-time flag

The `ens160.h` header provides `ENS160_ENABLE` (default 1). Set to 0 before including to skip all ENS160 code:
```c
#define ENS160_ENABLE 0
#include "ens160.h"
```
When disabled: ENS160 init, read, and set_env calls are compiled out. Temperature/humidity from AHT21 still works. Display shows "ENS warming..." placeholder.

## Server WiFi STA mode

The server connects to your home router in STA-only mode (no AP). STA credentials are stored in NVS namespace `wifi` with keys `sta_ssid` and `sta_pass`:

```bash
# Set STA credentials (one-time, via custom firmware or provisioning)
nvs_set_str("wifi", "sta_ssid", "MyHomeWiFi");
nvs_set_str("wifi", "sta_pass", "MyPassword");
```

If no STA credentials exist in NVS, the server uses credentials from Kconfig (`CONFIG_SERVER_STA_SSID`, `CONFIG_SERVER_STA_PASS`). If both are empty, the server will not connect to WiFi.

The `wifi_manager` automatically retries connection with different delays:
- Network not found: retry every 60 seconds
- Wrong password: retry every 3600 seconds (1 hour)
- Other errors: retry every 60 seconds

After 3 consecutive `NO_AP_FOUND` events, the manager automatically switches to the backup SSID (if configured). On next connection, it switches back to primary.

### UDP Broadcast Discovery

The server runs a UDP listener on port 5000 that responds to `AIRMON_DISCOVER` packets with `AIRMON_RESPONSE <ip>`. The client uses this to automatically find the server on the local network, regardless of which WiFi network (primary or backup) either device is connected to.

## Hardware

### Wemos D1 Mini

| Property | Value |
|---|---|
| MCU | ESP8266EX (Xtensa LX106, single core, silicon revision 1) |
| Flash | 4 MB external (connected as 2 MB in sdkconfig, QIO mode) |
| RAM | ~107 KB free at boot (IRAM + DRAM) |
| Clock | 80 MHz / 160 MHz (configured to **80 MHz** for power saving) |
| Crystal | 26 MHz |
| Voltage | 3.3 V logic (5 V tolerant on some pins via USB regulator) |
| USB | Micro-B (CH340G USB-to-serial) |
| Pinout | <https://docs.wemos.cc/en/latest/d1/d1_mini.html> |

Key GPIO pins available on headers: D0(GPIO16), D1(GPIO5), D2(GPIO4), D3(GPIO0), D4(GPIO2), D5(GPIO14), D6(GPIO12), D7(GPIO13), D8(GPIO15), RX(GPIO3), TX(GPIO1).

### ENS160+AHT21 Air Quality Sensor Module

| Property | Value |
|---|---|
| Sensors | ENS160 (eCO2, TVOC, AQI) + AHT21 (temperature, humidity) |
| Interface | I2C |
| ENS160 I2C address | `0x53` |
| AHT21 I2C address | `0x38` |
| Supply voltage | 3.3 V – 5 V (module has onboard regulator) |
| Logic level | 3.3 V I2C |
| Replaces | CCS811 (drop-in compatible form factor) |
| Datasheets | ENS160: <https://www.sciosense.com/products/environmental-sensors/ens160-gas-sensor/> ; AHT21: <https://www.aosong.com/product/aht21.html> |

**ENS160** measures equivalent CO2 (eCO2, ppm), total VOC (TVOC, ppb), and provides an Air Quality Index (AQI) rating (1–5). It requires a 48-hour initial burn-in period for accurate readings.

**AHT21** is a capacitive humidity sensor with integrated temperature sensing, ±2 % RH and ±0.3 °C accuracy.

Both sensors share the same I2C bus — each has a unique address, so they coexist without conflict.

### 0.91" OLED Display Module (SSD1306)

| Property | Value |
|---|---|
| Controller | SSD1306 |
| Resolution | **128 × 32 pixels** (physically 32 tall, NOT 64) |
| Interface | I2C |
| I2C address | `0x3C` (confirmed by I2C scan) |
| Supply voltage | 3.3 V – 5 V (module has onboard regulator) |
| Color | White (monochrome) |
| Datasheet | <https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf> |

**Critical:** The display is physically **128x32**, not 128x64. Pixels with y >= 32 are invisible. Always set `oled.height = 32`. Framebuffer size is `128 * 32 / 8 = 512` bytes.

#### Working SSD1306 initialization code

```cpp
#include "ssd1306.h"

extern "C" {
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "driver/i2c.h"
}

#define SDA_PIN 4
#define SCL_PIN 5
#define I2C_BUS I2C_NUM_0

static ssd1306_t oled;
static uint8_t fb[128 * 32 / 8];  // 512 bytes for 128x32

// In app_main():
i2c_config_t conf = {};
conf.mode = I2C_MODE_MASTER;
conf.sda_io_num = (gpio_num_t)SDA_PIN;   // GPIO4 = D2
conf.scl_io_num = (gpio_num_t)SCL_PIN;   // GPIO5 = D1
conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
conf.clk_stretch_tick = 300;
ESP_ERROR_CHECK(i2c_driver_install(I2C_BUS, conf.mode));
ESP_ERROR_CHECK(i2c_param_config(I2C_BUS, &conf));

oled.i2c_port = I2C_BUS;
oled.i2c_addr = SSD1306_I2C_ADDR_0;   // 0x3C
oled.screen = SSD1306_SCREEN;
oled.width = 128;
oled.height = 32;                       // MUST be 32, not 64

ssd1306_init(&oled);                    // Returns 0 on success, -5 (-EIO) if I2C fails
ssd1306_set_whole_display_lighting(&oled, false);
memset(fb, 0x00, sizeof(fb));

// ... draw into fb using ssd1306_draw_pixel, ssd1306_draw_string, etc. ...

ssd1306_load_frame_buffer(&oled, fb);   // Send fb to display
ssd1306_display_on(&oled, true);        // Turn display on
```

#### SSD1306 driver API (ssd1306.h)

Key functions that work:

| Function | Purpose |
|---|---|
| `ssd1306_init(&oled)` | Initialize display. Returns 0 on success, -5 (-EIO) on I2C failure |
| `ssd1306_display_on(&oled, bool on)` | Turn display on/off |
| `ssd1306_set_whole_display_lighting(&oled, bool light)` | All pixels on (true) or normal (false) |
| `ssd1306_set_contrast(&oled, uint8_t contrast)` | Set contrast 0–255 (default 0x9f) |
| `ssd1306_set_inversion(&oled, bool on)` | Invert display |
| `ssd1306_load_frame_buffer(&oled, fb)` | Send 512-byte framebuffer to display RAM |
| `ssd1306_clear_screen(&oled)` | Clear display RAM |
| `ssd1306_draw_pixel(&oled, fb, x, y, color)` | Set pixel. x: 0–127, y: 0–31 |
| `ssd1306_draw_hline(&oled, fb, x, y, w, color)` | Horizontal line |
| `ssd1306_draw_vline(&oled, fb, x, y, h, color)` | Vertical line |
| `ssd1306_draw_rectangle(&oled, fb, x, y, w, h, color)` | Rectangle outline |
| `ssd1306_fill_rectangle(&oled, fb, x, y, w, h, color)` | Filled rectangle |
| `ssd1306_draw_char(&oled, fb, font, x, y, c, fg, bg)` | Draw single character |
| `ssd1306_draw_string(&oled, fb, font, x, y, str, fg, bg)` | Draw string |

Colors: `OLED_COLOR_WHITE` (1), `OLED_COLOR_BLACK` (0), `OLED_COLOR_INVERT` (2), `OLED_COLOR_TRANSPARENT` (-1)

#### Scaled font rendering (2x for readability on 128x32)

GLCD 5x7 at 1x is too small. Use 2x scaling:

```cpp
static void draw_char_scaled(const font_info_t *font, int x0, int y0, char c, int scale) {
    const font_char_desc_t *d = font_get_char_desc(font, c);
    if (!d) return;
    const uint8_t *bitmap = font->bitmap + d->offset;
    int bytes_per_row = (d->width + 7) / 8;
    for (int y = 0; y < font->height; y++) {
        for (int x = 0; x < d->width; x++) {
            uint8_t byte_idx = bytes_per_row * y + x / 8;
            uint8_t bit_idx = 7 - (x % 8);
            if (bitmap[byte_idx] & (1 << bit_idx)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        ssd1306_draw_pixel(&oled, fb, x0 + x*scale + sx, y0 + y*scale + sy, OLED_COLOR_WHITE);
                    }
                }
            }
        }
    }
}

static void draw_string_scaled(const font_info_t *font, int x0, int y0, const char *str, int scale) {
    int x = x0;
    while (*str) {
        draw_char_scaled(font, x, y0, *str, scale);
        const font_char_desc_t *d = font_get_char_desc(font, *str);
        x += (d ? d->width * scale : scale);
        str++;
        if (*str) x += font->c * scale;
    }
}

// Usage: GLCD 5x7 at 2x → each char is ~12×14 px, ~2 lines fit on 128×32
const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];
draw_string_scaled(font, 0, 0, "LINE1", 2);   // y=0..13
draw_string_scaled(font, 0, 16, "LINE2", 2);   // y=16..29
```

#### Available fonts

| Enum | Name | Size | Char range | Notes |
|---|---|---|---|---|
| `FONT_FACE_GLCD5x7` | GLCD 5×7 | 5w×7h | 0–255 | Default, works; use with 2x scale |
| `FONT_FACE_COMPACT_6X8` | Compact 6×8 | 6w×8h | 0x20–0xFF | Latin + Cyrillic CP1251 |

#### I2C scanning (for debugging)

```cpp
for (int addr = 1; addr < 127; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_BUS, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) printf("I2C device found at 0x%02X\n", addr);
}
```

#### Serial debug output

Use `printf()` in `app_main()` — output appears on UART0 at 74880 baud. Read from Docker:

```python
python3 -c "
import serial, time
import sys
port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
ser = serial.Serial(port, 74880, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1); ser.rts = False; time.sleep(2)
buf = b''; deadline = time.time() + 5
while time.time() < deadline:
    data = ser.read(4096)
    if data: buf += data
    elif len(buf) > 0: break
ser.close(); print(buf.decode('utf-8', errors='replace'))
" /dev/ttyUSB0   # Client
# or: " /dev/ttyUSB1   # Server
```

**Note:** `idf.py monitor` does not work inside Docker (termios error). Use the Python script above instead.

## Wiring / I2C bus layout

All three devices share a **single I2C bus** (bus 0). The Wemos D1 Mini acts as I2C master.

```
Wemos D1 Mini                    I2C Bus (shared SDA + SCL)
┌─────────────┐                  ┌──────────────────────────────┐
│ 3V3         ├──────────────────┤ VCC (all modules)            │
│ GND         ├──────────────────┤ GND (all modules)            │
│ D2 (GPIO4)  ├──── SDA ────────┤ SDA → ENS160+AHT21, SSD1306  │
│ D1 (GPIO5)  ├──── SCL ────────┤ SCL → ENS160+AHT21, SSD1306  │
└─────────────┘                  └──────────────────────────────┘
```

| Connection | Wemos D1 Mini Pin | GPIO |
|---|---|---|
| I2C SDA | D2 | GPIO4 |
| I2C SCL | D1 | GPIO5 |
| 3.3 V power | 3V3 | — |
| Ground | GND | — |

**Important:** All modules operate at 3.3 V logic. The Wemos D1 Mini GPIO pins are 3.3 V, so no level shifters are needed. Power all modules from the Wemos 3V3 pin (check total current draw — OLED ~20 mA, sensors ~1 mA).

### Tactile Button (Display Wake)

| Property | Value |
|---|---|
| Type | Momentary tactile switch (NO - normally open) |
| GPIO | D6 (GPIO12) = input with internal pull-up, D5 (GPIO14) = output LOW |
| Wiring | Button connects D5 (GPIO14) and D6 (GPIO12) |
| Interrupt | GPIO_INTR_NEGEDGE (falling edge on press) |
| Active | Low (pressed = GPIO12 reads 0) |
| ISR | `vTaskNotifyGiveFromISR` → wakes `client_task` immediately |
| Behavior | Press → display ON for 6 seconds (2 screens × 3s), then auto-off |

```
Wemos D1 Mini
┌─────────────┐
│ D5 (GPIO14) ├────┐   output LOW (always GND)
│ D6 (GPIO12) ├────┤   input + pull-up, NEGEDGE interrupt
└─────────────┘    │
                ┌──┴──┐
                │ BTN │  (tactile switch, NO)
                └──┬──┘
                   │
```

GPIO12 (D6) is chosen because it has no special boot-strapping requirements. GPIO14 (D5) drives the button's ground side — no external resistor needed.

### I2C device addresses on the bus

| Device | I2C Address |
|---|---|
| SSD1306 OLED | `0x3C` |
| ENS160 | `0x53` |
| AHT21 | `0x38` |

No address conflicts exist. All three devices can coexist on the same bus.

## Architecture

- `apps/server/main/main.c` — Server entry: NVS → data_store → registry_load → WiFi (STA-only) → SNTP → HTTP server start → main loop with stale checker
- `apps/client/main/main.c` — Client entry: NVS → display_init → sensor init → storage init → WiFi sync init → separate FreeRTOS task `client_task` for main loop (sensor reads, display updates, WiFi sync)
- `components/display/` — C Display wrapper around `ssd1306.c` SSD1306 driver (third-party, MIT licensed)
- `components/sensors/` — ENS160 and AHT21 I2C sensor drivers
- `components/fonts/` — Compile-time bitmap font library (GLCD 5x7, Terminus, etc.)
- `components/wifi/` — WiFi manager: STA-only with retry logic (60s for network not found, 60min for auth fail)

## Configuration

- `sdkconfig` — ESP-IDF project configuration (gitignored, local only)
- ESP-IDF version: v3.4-110-gd412ac60
- Flash mode: QIO, 40 MHz
- Flash size: 4 MB physical (configured as 2 MB in sdkconfig — works fine)
- CPU frequency: 80 MHz
- Monitor baud: 74880

## Adding new I2C drivers

When adding drivers for ENS160 or AHT21:

1. Create C source/header in `components/sensors/` (e.g., `xyz.c`, `include/xyz.h`)
2. Register in `components/sensors/CMakeLists.txt` under `SRCS`
3. Use `i2c_master_cmd_begin(I2C_NUM_0, ...)` for I2C transactions
4. Include FreeRTOS/ESP headers in `extern "C" {}` blocks from C++ files
