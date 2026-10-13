#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "gesture.h"
esp_err_t usb_init(void);
bool usb_ready(void);
bool usb_send_mouse(const tp_report_t *r);
bool usb_readline(char *out, size_t capacity);
uint32_t usb_reports_sent(void);
uint32_t usb_reports_completed(void);
uint32_t usb_report_failures(void);
void usb_bootloader(void);
