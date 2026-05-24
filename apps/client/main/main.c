#include "Display.h"
#include "ens160.h"
#include "aht21.h"
#include "button.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_system.h"

static const char *TAG = "client";

#define SDA_PIN 4
#define SCL_PIN 5
#define I2C_BUS I2C_NUM_0

static Display display;
static bool display_on_flag = true;
static int display_page = 0;
static TickType_t last_button_time = 0;

void app_main(void) {
    ESP_LOGI(TAG, "Client starting...");

    // Initialize I2C bus
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)SDA_PIN;
    conf.scl_io_num = (gpio_num_t)SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.clk_stretch_tick = 300;
    ESP_ERROR_CHECK(i2c_driver_install(I2C_BUS, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(I2C_BUS, &conf));

    // Initialize display
    display_init(&display, SDA_PIN, SCL_PIN, SSD1306_I2C_ADDR_0);

    // Initialize sensors
    if (ens160_init(I2C_BUS) != 0) {
        ESP_LOGE(TAG, "Failed to initialize ENS160");
    }
    if (aht21_init(I2C_BUS) != 0) {
        ESP_LOGE(TAG, "Failed to initialize AHT21");
    }

    // Initialize button
    button_init();

    const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];

    // Show startup message
    display_clear_fb(&display);
    display_draw_string_scaled(&display, font, 0, 0, "Client", 2);
    display_draw_string_scaled(&display, font, 0, 17, "Starting...", 2);
    display_present(&display);
    display_on(&display);

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Main loop
    while (1) {
        // Read sensors
        aht21_data_t aht_data = {0};
        ens160_data_t ens_data = {0};

        if (aht21_read(I2C_BUS, &aht_data) != 0) {
            ESP_LOGW(TAG, "Failed to read AHT21");
        }
        if (ens160_read(I2C_BUS, &ens_data) != 0) {
            ESP_LOGW(TAG, "Failed to read ENS160");
        }

        // Update ENS160 with temperature/humidity compensation
        ens160_set_env(I2C_BUS, aht_data.temperature, aht_data.humidity);

        // Handle button press
        if (button_is_pressed()) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_button_time) > pdMS_TO_TICKS(500)) {
                display_page = (display_page + 1) % 3;
                display_on_flag = true;
                last_button_time = now;
            }
        }

        // Update display
        if (display_on_flag) {
            display_clear_fb(&display);

            char line1[32], line2[32];

            switch (display_page) {
                case 0:  // Temperature + Humidity
                    snprintf(line1, sizeof(line1), "%.1fC %.0f%%",
                             aht_data.temperature, aht_data.humidity);
                    snprintf(line2, sizeof(line2), "Temp/Hum");
                    break;
                case 1:  // eCO2 + TVOC + AQI
                    snprintf(line1, sizeof(line1), "%uppm %uppb",
                             ens_data.eco2, ens_data.tvoc);
                    snprintf(line2, sizeof(line2), "AQI:%u", ens_data.aqi);
                    break;
                case 2:  // System info
                    snprintf(line1, sizeof(line1), "Heap:%u", (unsigned int)esp_get_free_heap_size());
                    snprintf(line2, sizeof(line2), "Page:%d", display_page);
                    break;
            }

            display_draw_string_scaled(&display, font, 0, 0, line1, 2);
            display_draw_hline(&display, 0, 15, 128, OLED_COLOR_WHITE);
            display_draw_string_scaled(&display, font, 0, 17, line2, 2);
            display_present(&display);
            display_on(&display);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
