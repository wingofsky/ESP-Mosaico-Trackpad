#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
esp_err_t board_init(void);
esp_err_t board_display_init(void);
esp_err_t board_touch_init(void);
esp_err_t board_draw(const void *pixels);
esp_err_t board_brightness(unsigned percent);
esp_lcd_touch_handle_t board_touch(void);
unsigned board_version(void);
