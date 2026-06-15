#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "../sx1276/sx1276.h"
#include "../meshtastic/packet.h"
#include "../config.h"

static const char *TAG = "tx_task";

void lora_tx_task(void *pvParameters) {
    ESP_LOGI(TAG, "TX task started — NodeInfo broadcast every %d s",
             MESH_TX_INTERVAL_MS / 1000);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MESH_TX_INTERVAL_MS));

        uint8_t pkt_buf[255];
        size_t  pkt_len = packet_build_nodeinfo(g_node_addr, g_node_mac, pkt_buf);
        if (!pkt_len) {
            ESP_LOGE(TAG, "Failed to build NodeInfo packet");
            continue;
        }

        /* Switch to standby before writing FIFO */
        sx1276_set_mode(MODE_STDBY);
        vTaskDelay(pdMS_TO_TICKS(5));

        /* Load FIFO */
        sx1276_write_reg(REG_FIFO_ADDR_PTR, 0x00);
        sx1276_write_fifo(pkt_buf, (uint8_t)pkt_len);
        sx1276_write_reg(REG_PAYLOAD_LENGTH, (uint8_t)pkt_len);

        /* Start TX */
        sx1276_set_tx_mode();
        ESP_LOGI(TAG, "TX NodeInfo: src=0x%08" PRIx32 " id=%" PRIu32 " len=%zu",
                 g_node_addr, g_tx_packet_id - 1, pkt_len);

        /* Wait for TxDone interrupt (timeout = 5 s) */
        if (xSemaphoreTake(g_tx_done_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
            ESP_LOGI(TAG, "TxDone");
        } else {
            ESP_LOGW(TAG, "TxDone timeout — forcing standby");
            sx1276_set_mode(MODE_STDBY);
        }

        /* Update stats and post TX_DONE event */
        if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_stats.tx_count++;
            xSemaphoreGive(g_stats_mutex);
        }

        analyzer_event_t evt = {
            .type  = EVT_TX_DONE,
            .ts_ms = (uint32_t)(esp_timer_get_time() / 1000),
        };
        xQueueSend(g_event_queue, &evt, pdMS_TO_TICKS(100));

        /* Resume reception */
        sx1276_set_rx_mode();
    }
}
