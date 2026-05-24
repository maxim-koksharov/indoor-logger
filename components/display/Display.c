#include "Display.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "driver/i2c.h"

void display_init(Display *d, int sda_pin, int scl_pin, uint8_t addr)
{
    d->sda_pin = sda_pin;
    d->scl_pin = scl_pin;
    d->addr = addr;

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)sda_pin;
    conf.scl_io_num = (gpio_num_t)scl_pin;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.clk_stretch_tick = 300;
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));

    d->dev.i2c_port = I2C_NUM_0;
    d->dev.i2c_addr = addr;
    d->dev.screen = SSD1306_SCREEN;
    d->dev.width = 128;
    d->dev.height = 32;

    ssd1306_init(&d->dev);
    ssd1306_set_whole_display_lighting(&d->dev, false);
    memset(d->fb, 0x00, sizeof(d->fb));
}

void display_on(Display *d)
{
    ssd1306_display_on(&d->dev, true);
}

void display_off(Display *d)
{
    ssd1306_display_on(&d->dev, false);
}

void display_clear_fb(Display *d)
{
    memset(d->fb, 0x00, sizeof(d->fb));
}

void display_present(Display *d)
{
    ssd1306_load_frame_buffer(&d->dev, d->fb);
}

void display_draw_pixel(Display *d, int x, int y, ssd1306_color_t color)
{
    ssd1306_draw_pixel(&d->dev, d->fb, x, y, color);
}

void display_draw_hline(Display *d, int x, int y, int w, ssd1306_color_t color)
{
    ssd1306_draw_hline(&d->dev, d->fb, x, y, w, color);
}

void display_draw_vline(Display *d, int x, int y, int h, ssd1306_color_t color)
{
    ssd1306_draw_vline(&d->dev, d->fb, x, y, h, color);
}

void display_draw_char_scaled(Display *d, const font_info_t *font, int x0, int y0, char c, int scale)
{
    const font_char_desc_t *ch = font_get_char_desc(font, c);
    if (!ch) return;
    const uint8_t *bitmap = font->bitmap + ch->offset;
    int bytes_per_row = (ch->width + 7) / 8;
    for (int y = 0; y < font->height; y++) {
        for (int x = 0; x < ch->width; x++) {
            uint8_t byte_idx = bytes_per_row * y + x / 8;
            uint8_t bit_idx = 7 - (x % 8);
            if (bitmap[byte_idx] & (1 << bit_idx)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        display_draw_pixel(d, x0 + x * scale + sx, y0 + y * scale + sy, OLED_COLOR_WHITE);
                    }
                }
            }
        }
    }
}

void display_draw_string_scaled(Display *d, const font_info_t *font, int x0, int y0, const char *str, int scale)
{
    int x = x0;
    while (*str) {
        display_draw_char_scaled(d, font, x, y0, *str, scale);
        const font_char_desc_t *ch = font_get_char_desc(font, *str);
        x += (ch ? ch->width * scale : scale);
        if (*++str)
            x += font->c * scale;
    }
}
