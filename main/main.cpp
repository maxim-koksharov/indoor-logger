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
static uint8_t fb[128 * 64 / 8];

static const uint8_t A_bitmap[7] = {
    0b00100000,
    0b01010000,
    0b10001000,
    0b10001000,
    0b11111000,
    0b10001000,
    0b10001000,
};

static void draw_char_2x(int x0, int y0, const uint8_t *bmp, int w, int h) {
    for (int y = 0; y < h; y++) {
        uint8_t row = bmp[y];
        for (int x = 0; x < w; x++) {
            if (row & (0x80 >> x)) {
                ssd1306_draw_pixel(&oled, fb, x0 + x*2,     y0 + y*2,     OLED_COLOR_WHITE);
                ssd1306_draw_pixel(&oled, fb, x0 + x*2 + 1, y0 + y*2,     OLED_COLOR_WHITE);
                ssd1306_draw_pixel(&oled, fb, x0 + x*2,     y0 + y*2 + 1, OLED_COLOR_WHITE);
                ssd1306_draw_pixel(&oled, fb, x0 + x*2 + 1, y0 + y*2 + 1, OLED_COLOR_WHITE);
            }
        }
    }
}

extern "C" void app_main(void) {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)SDA_PIN;
    conf.scl_io_num = (gpio_num_t)SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.clk_stretch_tick = 300;
    ESP_ERROR_CHECK(i2c_driver_install(I2C_BUS, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(I2C_BUS, &conf));

    oled.i2c_port = I2C_BUS;
    oled.i2c_addr = SSD1306_I2C_ADDR_0;
    oled.screen = SSD1306_SCREEN;
    oled.width = 128;
    oled.height = 64;

    ssd1306_init(&oled);
    ssd1306_set_whole_display_lighting(&oled, false);

    memset(fb, 0x00, sizeof(fb));

    draw_char_2x(10, 10, A_bitmap, 5, 7);
    draw_char_2x(60, 10, A_bitmap, 5, 7);

    ssd1306_load_frame_buffer(&oled, fb);

    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
