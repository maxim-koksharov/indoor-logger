#pragma once

#include <stdint.h>
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AHT21_I2C_ADDR 0x38

typedef struct {
    float temperature;
    float humidity;
} aht21_data_t;

int aht21_init(i2c_port_t port);
int aht21_read(i2c_port_t port, aht21_data_t *data);

#ifdef __cplusplus
}
#endif
