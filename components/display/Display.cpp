#include "Display.hpp"

extern "C" {
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "driver/i2c.h"
}

#include "fonts.h"

Display::Display(int sda_pin, int scl_pin, uint8_t addr)
    : sda_pin_(sda_pin)
    , scl_pin_(scl_pin)
    , addr_(addr)
{
}

void Display::init()
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)sda_pin_;
    conf.scl_io_num = (gpio_num_t)scl_pin_;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.clk_stretch_tick = 300;
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));

    dev_.i2c_port = I2C_NUM_0;
    dev_.i2c_addr = addr_;
    dev_.screen = SSD1306_SCREEN;
    dev_.width = 128;
    dev_.height = 32;

    ssd1306_init(&dev_);
    ssd1306_set_whole_display_lighting(&dev_, false);
    memset(fb_, 0x00, sizeof(fb_));
}

void Display::on()
{
    ssd1306_display_on(&dev_, true);
}

void Display::off()
{
    ssd1306_display_on(&dev_, false);
}

void Display::clear_fb()
{
    memset(fb_, 0x00, sizeof(fb_));
}

void Display::present()
{
    ssd1306_load_frame_buffer(&dev_, fb_);
}

void Display::draw_pixel(int x, int y, ssd1306_color_t color)
{
    ssd1306_draw_pixel(&dev_, fb_, x, y, color);
}

void Display::draw_hline(int x, int y, int w, ssd1306_color_t color)
{
    ssd1306_draw_hline(&dev_, fb_, x, y, w, color);
}

void Display::draw_vline(int x, int y, int h, ssd1306_color_t color)
{
    ssd1306_draw_vline(&dev_, fb_, x, y, h, color);
}

void Display::draw_char_scaled(const font_info_t *font, int x0, int y0, char c, int scale)
{
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
                        draw_pixel(x0 + x * scale + sx, y0 + y * scale + sy, OLED_COLOR_WHITE);
                    }
                }
            }
        }
    }
}

void Display::draw_string_scaled(const font_info_t *font, int x0, int y0, const char *str, int scale)
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
