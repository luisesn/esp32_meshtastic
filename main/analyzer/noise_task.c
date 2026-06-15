#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "../sx1276/sx1276.h"
#include "../config.h"

static const char *TAG = "noise";

void noise_sampler_task(void *pvParameters) {
    ESP_LOGI(TAG, "Noise sampler started — every %d ms, N=%d samples",
             NOISE_INTERVAL_MS, NOISE_SAMPLE_COUNT);

    /* Initial delay to let the radio settle in RX mode */
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        /* sx1276_get_noise_floor() samples RSSI while no signal is detected
         * (checks ModemStat.signal_detected before each sample). */
        int16_t noise = sx1276_get_noise_floor();

        /* Update shared stats */
        if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_stats.last_noise_dbm = noise;
            xSemaphoreGive(g_stats_mutex);
        }

        analyzer_event_t evt = {
            .type                  = EVT_NOISE_SAMPLE,
            .ts_ms                 = (uint32_t)(esp_timer_get_time() / 1000),
            .noise.noise_floor_dbm = noise,
            .noise.sample_count    = NOISE_SAMPLE_COUNT,
        };
        xQueueSend(g_event_queue, &evt, pdMS_TO_TICKS(100));

        ESP_LOGD(TAG, "Noise floor: %d dBm", noise);
        vTaskDelay(pdMS_TO_TICKS(NOISE_INTERVAL_MS));
    }
}
