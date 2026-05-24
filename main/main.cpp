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
static uint8_t fb[128 * 32 / 8];

static void draw_char_scaled(const font_info_t *font, int x0, int y0, char c, int scale)
{
    const font_char_desc_t *d = font_get_char_desc(font, c);
    if (!d)
        return;
    const uint8_t *bitmap = font->bitmap + d->offset;
    int bytes_per_row = (d->width + 7) / 8;
    for (int y = 0; y < font->height; y++) {
        for (int x = 0; x < d->width; x++) {
            uint8_t byte_idx = bytes_per_row * y + x / 8;
            uint8_t bit_idx = 7 - (x % 8);
            if (bitmap[byte_idx] & (1 << bit_idx)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        ssd1306_draw_pixel(&oled, fb, x0 + x * scale + sx, y0 + y * scale + sy, OLED_COLOR_WHITE);
                    }
                }
            }
        }
    }
}

static void draw_string_scaled(const font_info_t *font, int x0, int y0, const char *str, int scale)
{
    int x = x0;
    while (*str) {
        draw_char_scaled(font, x, y0, *str, scale);
        const font_char_desc_t *d = font_get_char_desc(font, *str);
        x += (d ? d->width * scale : scale);
        if (*++str)
            x += font->c * scale;
    }
}

extern "C" void app_main(void)
{
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
    oled.height = 32;

    ssd1306_init(&oled);
    ssd1306_set_whole_display_lighting(&oled, false);
    memset(fb, 0x00, sizeof(fb));

    const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];
    draw_string_scaled(font, 0, 0, "25.5C 55%", 2);
    ssd1306_draw_hline(&oled, fb, 0, 15, 128, OLED_COLOR_WHITE);
    draw_string_scaled(font, 0, 17, "AQI 2 888pm", 2);

    ssd1306_load_frame_buffer(&oled, fb);
    ssd1306_display_on(&oled, true);

    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
