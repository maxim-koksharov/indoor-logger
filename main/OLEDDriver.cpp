// components/main/OLEDDriver.cpp

#include "OLEDDriver.hpp"

// Обязательная обертка для C-заголовков SDK
extern "C" {
    #include "driver/i2c.h"
    #include "esp_log.h"
    #include "fonts/fonts.h" // Подключаем шрифты
}

static const char *TAG = "OLED_DRIVER_CPP";

OLEDDriver::OLEDDriver(int sda_pin, int scl_pin, uint8_t i2c_addr)
    : _sda_pin(sda_pin), _scl_pin(scl_pin), _i2c_addr(i2c_addr) 
{
    ESP_LOGI(TAG, "OLEDDriver создан.");
}

void OLEDDriver::initialize() {
    // 1. Инициализация I2C мастера (нативный вызов SDK)
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = static_cast<gpio_num_t>(_sda_pin);
    conf.scl_io_num = static_cast<gpio_num_t>(_scl_pin);
    conf.sda_pullup_en = static_cast<gpio_pullup_t>(1);
    conf.scl_pullup_en = static_cast<gpio_pullup_t>(1);
    conf.clk_stretch_tick = 300;
    
    i2c_driver_install(I2C_NUM_0, conf.mode);
    i2c_param_config(I2C_NUM_0, &conf);

    for(uint32_t i = 0; i < oled_size; i++) {
        framebuffer[i] = 0;
    }

    // 2. Инициализация C-структуры дисплея
    dev.i2c_port = I2C_NUM_0;
    dev.i2c_addr = _i2c_addr;
    dev.screen = SSD1306_SCREEN; // 128x64 или 128x32, зависит от вашего ssd1306.h
    dev.width = 128;
    dev.height = 64;

    // 3. Инициализация самого дисплея через C-драйвер
    ssd1306_init(&dev);
    ssd1306_set_contrast(&dev, 0xff);
    
    ESP_LOGI(TAG, "Дисплей SSD1306 инициализирован.");
    clear();
    // refresh();
}

void OLEDDriver::clear() {
    ssd1306_clear_screen(&dev);
}

void OLEDDriver::draw_circle(uint8_t x, uint8_t y, uint8_t r) {
    ssd1306_draw_circle(&dev, framebuffer, x, y, r, OLED_COLOR_WHITE);
}
