#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef CONFIG_BT_ENABLED
/* Initialize Bluetooth Classic SPP.  Call once from app_main before tasks start. */
esp_err_t bt_spp_init(const char *device_name);
/* Write to connected SPP client.  No-op if disconnected or congested. */
void bt_spp_write(const uint8_t *data, uint16_t len);
#else
static inline esp_err_t bt_spp_init(const char *n) { (void)n; return ESP_OK; }
static inline void bt_spp_write(const uint8_t *d, uint16_t l) { (void)d; (void)l; }
#endif
