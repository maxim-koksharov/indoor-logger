#include "ens160.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ens160";

#define ENS160_REG_PART_ID   0x00
#define ENS160_REG_OPMODE    0x10
#define ENS160_REG_COMMAND   0x12
#define ENS160_REG_DATA_STATUS 0x20
#define ENS160_REG_DATA_AQI  0x21
#define ENS160_REG_DATA_TVOC 0x22
#define ENS160_REG_DATA_ECO2 0x24
#define ENS160_REG_TEMP_IN   0x26
#define ENS160_REG_RH_IN     0x28

static esp_err_t ens160_write_reg(i2c_port_t port, uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ENS160_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ens160_read_regs(i2c_port_t port, uint8_t reg, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ENS160_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ENS160_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

int ens160_init(i2c_port_t port) {
    uint8_t part_id[2];
    esp_err_t ret = ens160_read_regs(port, ENS160_REG_PART_ID, part_id, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read PART_ID");
        return -1;
    }
    uint16_t pid = part_id[0] | (part_id[1] << 8);
    ESP_LOGI(TAG, "ENS160 PART_ID: 0x%04X", pid);
    if (pid != 0x0160) {
        ESP_LOGE(TAG, "Invalid PART_ID: 0x%04X", pid);
        return -1;
    }

    ret = ens160_write_reg(port, ENS160_REG_OPMODE, 0x01);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set OPMODE");
        return -1;
    }

    ESP_LOGI(TAG, "ENS160 initialized");
    return 0;
}

int ens160_set_env(i2c_port_t port, float temperature, float humidity) {
    uint16_t temp_k = (uint16_t)((temperature + 273.15f) * 64.0f);
    uint16_t rh = (uint16_t)(humidity * 512.0f);

    uint8_t temp_data[2] = { (uint8_t)(temp_k & 0xFF), (uint8_t)(temp_k >> 8) };
    uint8_t rh_data[2] = { (uint8_t)(rh & 0xFF), (uint8_t)(rh >> 8) };

    esp_err_t ret = ens160_write_reg(port, ENS160_REG_TEMP_IN, temp_data[0]);
    if (ret != ESP_OK) return -1;
    ret = ens160_write_reg(port, ENS160_REG_TEMP_IN + 1, temp_data[1]);
    if (ret != ESP_OK) return -1;

    ret = ens160_write_reg(port, ENS160_REG_RH_IN, rh_data[0]);
    if (ret != ESP_OK) return -1;
    ret = ens160_write_reg(port, ENS160_REG_RH_IN + 1, rh_data[1]);
    if (ret != ESP_OK) return -1;

    return 0;
}

int ens160_read(i2c_port_t port, ens160_data_t *data) {
    uint8_t status;
    esp_err_t ret = ens160_read_regs(port, ENS160_REG_DATA_STATUS, &status, 1);
    if (ret != ESP_OK) return -1;

    if (!(status & 0x02)) {
        return -2;
    }

    uint8_t buf[5];
    ret = ens160_read_regs(port, ENS160_REG_DATA_AQI, buf, 5);
    if (ret != ESP_OK) return -1;

    data->aqi = buf[0];
    data->tvoc = buf[1] | (buf[2] << 8);
    data->eco2 = buf[3] | (buf[4] << 8);

    return 0;
}
