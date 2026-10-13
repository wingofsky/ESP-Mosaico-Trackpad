idf_component_register(SRCS "main.c" "board.c" "usb.c" "gesture.c" "ui.c" "command_line.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_lcd_co5300 esp_lcd_touch esp_lcd_touch_cst9220
             esp_tinyusb tinyusb esp_driver_gpio esp_driver_spi esp_driver_i2c
             esp_driver_ledc esp_timer efuse esp_psram esp_app_format)
