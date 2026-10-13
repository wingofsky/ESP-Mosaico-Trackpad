#pragma once
#include "esp_err.h"
#include "gesture.h"
typedef struct { bool connected, fault; uint8_t buttons; tp_input_t input; } ui_state_t;
esp_err_t ui_init(void);
void ui_update(const ui_state_t *state);
