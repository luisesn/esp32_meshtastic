#pragma once
#include "esp_err.h"

esp_err_t oled_init(void);
void oled_clear(void);
void oled_puts(uint8_t col, uint8_t row, const char *str);
void oled_flush(void);
