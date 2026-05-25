# Sensors — ENS160 + AHT21 I2C Drivers

I2C sensor drivers for the ENS160 air quality sensor and AHT21 temperature/humidity sensor.

## Files

| File | Description |
|---|---|
| `ens160.c` / `include/ens160.h` | ENS160 driver (eCO₂, TVOC, AQI) |
| `aht21.c` / `include/aht21.h` | AHT21 driver (temperature, humidity) |

## Dependencies

- `driver/i2c` (ESP-IDF)

## ENS160 — Air Quality Sensor

| Property | Value |
|---|---|
| I2C address | `0x53` |
| Measurements | eCO₂ (ppm), TVOC (ppb), AQI (1–5) |
| Warm-up | ~3 minutes for first reading |
| Stabilization | ~1 hour nominal, 48 hours for best accuracy |
| Operating mode | `0x02` (STANDARD) — must NOT be `0x01` (IDLE) |

### ENS160_ENABLE

Compile out ENS160 support to save code size or when only AHT21 is needed:

```c
#define ENS160_ENABLE 0
#include "ens160.h"
```

Default is 1 (enabled).

## AHT21 — Temperature & Humidity

| Property | Value |
|---|---|
| I2C address | `0x38` |
| Measurements | Temperature (°C), humidity (% RH) |
| Accuracy | ±0.3 °C, ±2% RH |

## Usage

```c
#include "ens160.h"
#include "aht21.h"

// Init (in app_main)
ens160_init(I2C_NUM_0);
aht21_init(I2C_NUM_0);

// Read every 5 seconds
aht21_data_t aht = {0};
ens160_data_t ens = {0};

int aht_ret = aht21_read(I2C_NUM_0, &aht);
if (aht_ret == 0) {
    // aht.temperature, aht.humidity
}

int ens_ret = ens160_read(I2C_NUM_0, &ens);
if (ens_ret == 0) {
    // ens.eco2, ens.tvoc, ens.aqi
}

// Feed temperature/humidity to ENS160 for compensation
ens160_set_env(I2C_NUM_0, aht.temperature, aht.humidity);
```

## ENS160 Status Codes

| Return | Meaning |
|---|---|
| `0` | Success, new data valid |
| `-1` | I2C communication error |
| `-2` | No new data ready (retry after 50–100 ms) |

## ESP8266 Note

Do NOT use `%f` in `printf`/`ESP_LOGI` when printing sensor values. Use integer arithmetic:
```c
int t_int = (int)temperature;
int t_dec = (int)(temperature * 10) % 10;
ESP_LOGI(TAG, "T=%d.%d", t_int, t_dec);
```
