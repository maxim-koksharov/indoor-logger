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
| Flash | 2 MB external |
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
| Resolution | 128 × 64 pixels |
| Interface | I2C |
| I2C address | `0x3C` (default) or `0x3D` (if A0 pin pulled high) |
| Supply voltage | 3.3 V – 5 V (module has onboard regulator) |
| Color | White (monochrome) |
| Datasheet | <https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf> |

Note: The current `ssd1306.h` defines `SSD1306_SCREEN` for 128×64. The 128×32 display uses the same controller but with `height = 32`. Update `oled.height` accordingly in code.

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
- Flash size: 2 MB
- CPU frequency: 160 MHz
- Monitor baud: 74880

## Adding new I2C drivers

When adding drivers for ENS160 or AHT21:

1. Create C source/header in `main/` (e.g., `ens160.c`, `ens160.h`)
2. Register in `main/CMakeLists.txt` under `SRCS`
3. Use `i2c_master_cmd_begin(I2C_NUM_0, ...)` for I2C transactions
4. Include FreeRTOS/ESP headers in `extern "C" {}` blocks from C++ files
