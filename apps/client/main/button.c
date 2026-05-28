#include "button.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_sleep.h"

static TaskHandle_t button_task = NULL;

void IRAM_ATTR button_isr_handler(void *arg) {
    TaskHandle_t task = (TaskHandle_t)arg;
    if (task == NULL) return;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
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

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)BUTTON_GPIO, button_isr_handler, (void*)NULL);
}

void button_set_task_handle(TaskHandle_t task) {
    button_task = task;
    gpio_isr_handler_add((gpio_num_t)BUTTON_GPIO, button_isr_handler, (void*)button_task);
}

bool button_is_pressed(void) {
    int level = gpio_get_level((gpio_num_t)BUTTON_GPIO);
#if BUTTON_ACTIVE_LOW
    return (level == 0);
#else
    return (level == 1);
#endif
}

void button_enable_wakeup(void) {
    gpio_wakeup_enable((gpio_num_t)BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void button_disable_wakeup(void) {
    gpio_wakeup_disable((gpio_num_t)BUTTON_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}
