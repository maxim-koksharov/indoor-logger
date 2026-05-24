#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ssd1306.h"
#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ssd1306_t dev;
    uint8_t fb[128 * 32 / 8];
    int sda_pin;
    int scl_pin;
    uint8_t addr;
} Display;

void display_init(Display *d, int sda_pin, int scl_pin, uint8_t addr);
void display_on(Display *d);
void display_off(Display *d);
void display_clear_fb(Display *d);
void display_present(Display *d);
void display_draw_pixel(Display *d, int x, int y, ssd1306_color_t color);
void display_draw_hline(Display *d, int x, int y, int w, ssd1306_color_t color);
void display_draw_vline(Display *d, int x, int y, int h, ssd1306_color_t color);
void display_draw_char_scaled(Display *d, const font_info_t *font, int x, int y, char c, int scale);
void display_draw_string_scaled(Display *d, const font_info_t *font, int x, int y, const char *str, int scale);

#ifdef __cplusplus
}
#endif
