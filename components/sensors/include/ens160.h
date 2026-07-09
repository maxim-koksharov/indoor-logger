#pragma once

#include <stdint.h>
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENS160_I2C_ADDR 0x53

#ifndef ENS160_ENABLE
#define ENS160_ENABLE 1
#endif

typedef struct {
    uint8_t aqi;
    uint16_t eco2;
    uint16_t tvoc;
} ens160_data_t;

int ens160_init(i2c_port_t port);
int ens160_read(i2c_port_t port, ens160_data_t *data);
int ens160_set_env(i2c_port_t port, float temperature, float humidity);

#ifdef __cplusplus
}
#endif
