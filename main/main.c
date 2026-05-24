#include "Display.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SDA_PIN 4
#define SCL_PIN 5

static Display display;

void app_main(void)
{
    display_init(&display, SDA_PIN, SCL_PIN, SSD1306_I2C_ADDR_0);

    const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];

    display_clear_fb(&display);
    display_draw_string_scaled(&display, font, 0, 0, "25.5C 55%", 2);
    display_draw_hline(&display, 0, 15, 128, OLED_COLOR_WHITE);
    display_draw_string_scaled(&display, font, 0, 17, "AQI 2 888pm", 2);

    display_present(&display);
    display_on(&display);

    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
