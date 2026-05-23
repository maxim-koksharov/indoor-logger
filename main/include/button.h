#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_GPIO       12
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_ACTIVE_LOW  1

void button_init(void);
bool button_is_pressed(void);

#ifdef __cplusplus
}
#endif
