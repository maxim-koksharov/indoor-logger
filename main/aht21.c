#include "aht21.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aht21";

#define AHT21_CMD_INIT   0xBE
#define AHT21_CMD_TRIGGER 0xAC
#define AHT21_CMD_STATUS  0x71

static esp_err_t aht21_write(i2c_port_t port, const uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT21_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t aht21_read_raw(i2c_port_t port, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT21_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

int aht21_init(i2c_port_t port) {
    uint8_t cmd[] = { AHT21_CMD_INIT, 0x08, 0x00 };
    esp_err_t ret = aht21_write(port, cmd, 3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send init command");
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t status;
    ret = aht21_read_raw(port, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status");
        return -1;
    }

    if (!(status & 0x18)) {
        ESP_LOGE(TAG, "AHT21 not calibrated");
        return -1;
    }

    ESP_LOGI(TAG, "AHT21 initialized, status: 0x%02X", status);
    return 0;
}

int aht21_read(i2c_port_t port, aht21_data_t *data) {
    uint8_t cmd[] = { AHT21_CMD_TRIGGER, 0x33, 0x00 };
    esp_err_t ret = aht21_write(port, cmd, 3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger measurement");
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t buf[7];
    ret = aht21_read_raw(port, buf, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data");
        return -1;
    }

    uint32_t hum_raw = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
    uint32_t temp_raw = ((uint32_t)(buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];

    data->humidity = (float)hum_raw / (1 << 20) * 100.0f;
    data->temperature = (float)temp_raw / (1 << 20) * 200.0f - 50.0f;

    return 0;
}
