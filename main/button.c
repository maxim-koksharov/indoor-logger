#include "button.h"
#include "driver/gpio.h"

void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

bool button_is_pressed(void) {
    int level = gpio_get_level((gpio_num_t)BUTTON_GPIO);
#if BUTTON_ACTIVE_LOW
    return (level == 0);
#else
    return (level == 1);
#endif
}
