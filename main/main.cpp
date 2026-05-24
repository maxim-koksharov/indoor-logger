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
    printf("=== OLED TEST START ===\n");

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)SDA_PIN;
    conf.scl_io_num = (gpio_num_t)SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.clk_stretch_tick = 300;

    printf("I2C init...\n");
    esp_err_t err = i2c_driver_install(I2C_BUS, conf.mode);
    printf("i2c_driver_install: %d\n", err);
    err = i2c_param_config(I2C_BUS, &conf);
    printf("i2c_param_config: %d\n", err);

    oled.i2c_port = I2C_BUS;
    oled.i2c_addr = SSD1306_I2C_ADDR_0;
    oled.screen = SSD1306_SCREEN;
    oled.width = 128;
    oled.height = 32;

    printf("Scanning I2C bus...\n");
    for (int addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_BUS, cmd, 100 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            printf("I2C device found at 0x%02X\n", addr);
        }
    }
    printf("I2C scan done\n");

    printf("SSD1306 init...\n");
    err = ssd1306_init(&oled);
    printf("ssd1306_init: %d\n", err);

    ssd1306_set_whole_display_lighting(&oled, false);

    memset(fb, 0x00, sizeof(fb));

    for (int i = 0; i < 32; i++) {
        ssd1306_draw_pixel(&oled, fb, i, i, OLED_COLOR_WHITE);
        ssd1306_draw_pixel(&oled, fb, 127 - i, i, OLED_COLOR_WHITE);
    }

    const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];
    printf("font: height=%d, c=%d, char_start=%d, char_end=%d\n",
           font->height, font->c, font->char_start, font->char_end);
    draw_string_scaled(font, 40, 4, "HELLO", 2);

    printf("Loading framebuffer...\n");
    err = ssd1306_load_frame_buffer(&oled, fb);
    printf("load_frame_buffer: %d\n", err);

    printf("Display ON\n");
    ssd1306_display_on(&oled, true);

    printf("=== OLED TEST DONE ===\n");

    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}