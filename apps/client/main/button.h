#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wiring: D5 (GPIO14) = output LOW, D6 (GPIO12) = input with pull-up
// Button connects D5 and D6. When pressed, D6 reads LOW (falling edge).
#define BUTTON_GPIO       12
#define BUTTON_GPIO_GND   14
#define BUTTON_ACTIVE_LOW  1

void button_init(void);
void button_set_task_handle(TaskHandle_t task);
bool button_is_pressed(void);
bool button_was_pressed(void);
bool button_is_pressed_flag(void);
void button_set_pressed_flag(void);
uint32_t button_get_isr_count(void);
bool button_is_pressed_debounced(void);
void button_enable_wakeup(void);
void button_disable_wakeup(void);

#ifdef __cplusplus
}
#endif
