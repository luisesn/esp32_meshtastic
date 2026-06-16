#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "../sx1276/sx1276.h"
#include "../meshtastic/packet.h"
#include "../config.h"

static const char *TAG = "tx_task";

/* ── Low-level TX helper ─────────────────────────────────────────────────── */

static void do_tx(const uint8_t *pkt, size_t len, const char *desc) {
    sx1276_set_mode(MODE_STDBY);
    vTaskDelay(pdMS_TO_TICKS(5));

    sx1276_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    sx1276_write_fifo(pkt, (uint8_t)len);
    sx1276_write_reg(REG_PAYLOAD_LENGTH, (uint8_t)len);
    sx1276_set_tx_mode();

    ESP_LOGI(TAG, "TX %s: %zu bytes", desc, len);

    if (xSemaphoreTake(g_tx_done_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
        ESP_LOGI(TAG, "TxDone");
    } else {
        ESP_LOGW(TAG, "TxDone timeout — forcing standby");
        sx1276_set_mode(MODE_STDBY);
    }

    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_stats.tx_count++;
        xSemaphoreGive(g_stats_mutex);
    }

    analyzer_event_t evt = {
        .type  = EVT_TX_DONE,
        .ts_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    xQueueSend(g_event_queue, &evt, pdMS_TO_TICKS(100));

    sx1276_set_rx_mode();
}

/* ── Task entry ──────────────────────────────────────────────────────────── */

void lora_tx_task(void *pvParameters) {
    ESP_LOGI(TAG, "TX task started — NodeInfo every %d s, on-demand TX via queue",
             MESH_TX_INTERVAL_MS / 1000);

    TickType_t last_nodeinfo = xTaskGetTickCount();

    while (1) {
        TickType_t now      = xTaskGetTickCount();
        TickType_t elapsed  = now - last_nodeinfo;
        TickType_t interval = pdMS_TO_TICKS(MESH_TX_INTERVAL_MS);

        /* How long until next NodeInfo broadcast */
        TickType_t wait = (elapsed >= interval) ? 0 : (interval - elapsed);

        lora_tx_pkt_t req;
        if (xQueueReceive(g_tx_request_queue, &req, wait) == pdTRUE) {
            /* On-demand packet (e.g. position) */
            do_tx(req.data, req.len, "on-demand");
        } else {
            /* NodeInfo timer fired */
            uint8_t pkt_buf[255];
            size_t  pkt_len = packet_build_nodeinfo(g_node_addr, g_node_mac, pkt_buf);
            if (pkt_len)
                do_tx(pkt_buf, pkt_len, "NodeInfo");
            last_nodeinfo = xTaskGetTickCount();
        }
    }
}
