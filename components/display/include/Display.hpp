#pragma once

#include <stdint.h>
#include "ssd1306.h"

class Display {
public:
    Display(int sda_pin, int scl_pin, uint8_t addr = SSD1306_I2C_ADDR_0);

    void init();
    void on();
    void off();
    void clear_fb();
    void present();

    void draw_pixel(int x, int y, ssd1306_color_t color);
    void draw_hline(int x, int y, int w, ssd1306_color_t color);
    void draw_vline(int x, int y, int h, ssd1306_color_t color);

    void draw_char_scaled(const font_info_t *font, int x, int y, char c, int scale);
    void draw_string_scaled(const font_info_t *font, int x, int y, const char *str, int scale);

    int width() const { return dev_.width; }
    int height() const { return dev_.height; }

private:
    ssd1306_t dev_;
    uint8_t fb_[128 * 32 / 8];
    int sda_pin_;
    int scl_pin_;
    uint8_t addr_;
};
