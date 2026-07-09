#include "button.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DEBOUNCE_TIME_MS 50
#define LONG_PRESS_IGNORE_MS 5000

static const char *TAG = "button";
static bool last_button_state = false;
static TickType_t last_state_change_time = 0;
static bool debounced_state = false;
static bool long_press_ignore = false;
static TickType_t press_start_time = 0;
static bool press_reported = false;

void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_config_t gnd_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO_GND),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gnd_conf);
    gpio_set_level((gpio_num_t)BUTTON_GPIO_GND, 0);

    vTaskDelay(pdMS_TO_TICKS(100));

    last_button_state = button_is_pressed();
    debounced_state = last_button_state;
    long_press_ignore = false;
    press_reported = false;
}

void button_set_task_handle(TaskHandle_t task) {
    (void)task;
}

bool button_is_pressed(void) {
    int level = gpio_get_level((gpio_num_t)BUTTON_GPIO);
#if BUTTON_ACTIVE_LOW
    return (level == 0);
#else
    return (level == 1);
#endif
}

bool button_was_pressed(void) {
    return false;
}

bool button_is_pressed_flag(void) {
    return false;
}

void button_set_pressed_flag(void) {
}

uint32_t button_get_isr_count(void) {
    return 0;
}

bool button_is_pressed_debounced(void) {
    bool current_state = button_is_pressed();
    TickType_t now = xTaskGetTickCount();
    bool result = false;

    if (current_state != last_button_state) {
        last_button_state = current_state;
        last_state_change_time = now;
        if (current_state) {
            press_start_time = now;
            // Force a fresh debounce cycle for this new press. This handles
            // the case where the user releases after a long press and quickly
            // presses again while debounced_state was still true.
            debounced_state = false;
            press_reported = false;
            long_press_ignore = false;
        } else {
            long_press_ignore = false;
            press_reported = false;
        }
    } else if ((now - last_state_change_time) >= pdMS_TO_TICKS(DEBOUNCE_TIME_MS)) {
        if (debounced_state != current_state) {
            debounced_state = current_state;
        }
        if (debounced_state && !press_reported && !long_press_ignore) {
            press_reported = true;
            result = true;
        }
    }

    if (debounced_state && (now - press_start_time) >= pdMS_TO_TICKS(LONG_PRESS_IGNORE_MS)) {
        long_press_ignore = true;
    }

    if (!debounced_state) {
        long_press_ignore = false;
        press_reported = false;
    }

    return result;
}

void button_enable_wakeup(void) {
    gpio_wakeup_enable((gpio_num_t)BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void button_disable_wakeup(void) {
    gpio_wakeup_disable((gpio_num_t)BUTTON_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}
