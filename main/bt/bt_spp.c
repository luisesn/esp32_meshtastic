#include "bt_spp.h"

#ifdef CONFIG_BT_ENABLED

#include <stdatomic.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_log.h"

static const char *TAG = "bt_spp";

/* Atomic so logger_task reads are safe without a mutex */
static atomic_uint_fast32_t s_handle    = 0;
static atomic_bool           s_congested = false;

static void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "SPP init OK — starting server");
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "TLoRA");
        } else {
            ESP_LOGE(TAG, "SPP init failed: %d", param->init.status);
        }
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        if (param->srv_open.status == ESP_SPP_SUCCESS) {
            atomic_store(&s_handle, param->srv_open.handle);
            atomic_store(&s_congested, false);
            ESP_LOGI(TAG, "BT client connected, handle=%" PRIu32, param->srv_open.handle);
        }
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "BT client disconnected");
        atomic_store(&s_handle, 0);
        break;
    case ESP_SPP_CONG_EVT:
        atomic_store(&s_congested, param->cong.cong);
        break;
    default:
        break;
    }
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    (void)event; (void)param;
}

esp_err_t bt_spp_init(const char *device_name) {
    esp_err_t ret;

    /* Release BLE RAM — Classic BT only */
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "BLE mem release: %s", esp_err_to_name(ret));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_bt_gap_register_callback(gap_callback);
    esp_spp_register_callback(spp_callback);

    esp_bt_gap_set_device_name(device_name);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    esp_spp_cfg_t spp_cfg = {
        .mode             = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size   = 0,
    };
    ret = esp_spp_enhanced_init(&spp_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP enhanced init: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BT SPP init done — device name: %s", device_name);
    return ESP_OK;
}

void bt_spp_write(const uint8_t *data, uint16_t len) {
    uint32_t h = (uint32_t)atomic_load(&s_handle);
    if (h == 0 || atomic_load(&s_congested) || len == 0) return;
    esp_spp_write(h, (int)len, (uint8_t *)data);
}

#endif /* CONFIG_BT_ENABLED */
