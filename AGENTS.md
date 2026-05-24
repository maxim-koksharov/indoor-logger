# AGENTS.md

## Project overview

ESP8266 embedded firmware (ESP8266 RTOS SDK / ESP-IDF style) that drives an SSD1306 OLED display over I2C to log indoor environment data (eCO2, TVOC, temperature, humidity) from an ENS160+AHT21 sensor module. C++ with C interop, but add only C++ code, if it's impossible add C code.

## Development environment

**All development runs inside a Docker container** based on `ubuntu:20.04` (image: `esp8266-env`).

- Container image built from `Dockerfile` at project root
- Project directory mounted at `/esp/project`
- SDK located at `/esp/ESP8266_RTOS_SDK`
- Toolchain at `/esp/bin/xtensa-lx106-elf/bin`
- USB serial device passed through as `/dev/ttyUSB0`
- Launch opencode in docker (from host system) via `./run_opencode_docker.sh`

## Build system

**CMake only.** The `Makefile` at project root is a leftover wrapper — do not use it.

### Setup

```bash
export IDF_PATH=/esp/ESP8266_RTOS_SDK
```

This is already set in the Docker image via `ENV` and `/etc/bash.bashrc`.

### Commands

| Action | Command |
|---|---|
| Configure | `idf.py menuconfig` |
| Build | `idf.py build` |
| Flash | `idf.py -p /dev/ttyUSB0 flash` |
| Monitor | `idf.py -p /dev/ttyUSB0 monitor` |
| Flash + Monitor | `idf.py -p /dev/ttyUSB0 flash monitor` |
| Erase flash | `idf.py -p /dev/ttyUSB0 erase_flash` |

The `xtensa-lx106-elf-*` toolchain binaries are already in `PATH`.

## Entry point

`app_main()` in `main/main.cpp` — **not** `user_init` (README is outdated on this point).

All FreeRTOS and ESP SDK headers must be wrapped in `extern "C" {}` when included from C++ files.

If you need to verify your changes, use command "Flash". Flash device only with Flash command.

## Hardware

### Wemos D1 Mini

| Property | Value |
|---|---|
| MCU | ESP8266EX (Xtensa LX106, single core, silicon revision 1) |
| Flash | 4 MB external (connected as 2 MB in sdkconfig, QIO mode) |
| RAM | ~107 KB free at boot (IRAM + DRAM) |
| Clock | 80 MHz / 160 MHz (configured to 160 MHz) |
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
ser = serial.Serial('/dev/ttyUSB0', 74880, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1); ser.rts = False; time.sleep(2)
buf = b''; deadline = time.time() + 5
while time.time() < deadline:
    data = ser.read(4096)
    if data: buf += data
    elif len(buf) > 0: break
ser.close(); print(buf.decode('utf-8', errors='replace'))
"
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
| GPIO | D6 (GPIO12) |
| Wiring | One leg → GPIO12, other leg → GND |
| Pull-up | Internal pull-up resistor enabled |
| Active | Low (pressed = GPIO reads 0) |
| Debounce | 50 ms software debounce |
| Behavior | Press → display ON for 10 seconds, then auto-off |

```
Wemos D1 Mini
┌─────────────┐
│ D6 (GPIO12) ├────┐
│ GND         ├────┤
└─────────────┘    │
                ┌──┴──┐
                │ BTN │  (tactile switch)
                └──┬──┘
                   │
                 (both legs connected as shown)
```

**Note:** GPIO12 (D6) is chosen because it has no special boot-strapping requirements. Avoid GPIO0 (D3) as it affects boot mode.

### I2C device addresses on the bus

| Device | I2C Address |
|---|---|
| SSD1306 OLED | `0x3C` |
| ENS160 | `0x53` |
| AHT21 | `0x38` |

No address conflicts exist. All three devices can coexist on the same bus.

## Architecture

- `main/main.cpp` — app entry point, currently initializes I2C bus and SSD1306 via C driver
- `main/ssd1306.c` + `main/include/ssd1306.h` — C SSD1306 driver (third-party, MIT licensed)
- `main/OLEDDriver.cpp` + `main/include/OLEDDriver.hpp` — C++ wrapper around the C driver (incomplete/unused)
- `main/fonts/` — compile-time font library; font selection via flags in `main/fonts/defaults.mk`; font data generated by `main/fonts/tools/create_font.py`
- `main/CMakeLists.txt` — registers `main.cpp`, `ssd1306.c`, and `fonts/fonts.c` as component sources

## Configuration

- `sdkconfig` — ESP-IDF project configuration (gitignored, local only)
- ESP-IDF version: v3.4-110-gd412ac60
- Serial port: `/dev/ttyUSB0`
- Flash mode: QIO, 40 MHz
- Flash size: 4 MB physical (configured as 2 MB in sdkconfig — works fine)
- CPU frequency: 160 MHz
- Monitor baud: 74880

## Adding new I2C drivers

When adding drivers for ENS160 or AHT21:

1. Create C source/header in `main/` (e.g., `ens160.c`, `ens160.h`)
2. Register in `main/CMakeLists.txt` under `SRCS`
3. Use `i2c_master_cmd_begin(I2C_NUM_0, ...)` for I2C transactions
4. Include FreeRTOS/ESP headers in `extern "C" {}` blocks from C++ files
