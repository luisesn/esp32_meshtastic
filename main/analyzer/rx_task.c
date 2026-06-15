#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "../sx1276/sx1276.h"
#include "../meshtastic/packet.h"
#include "../meshtastic/crypto.h"
#include "../meshtastic/proto/proto_encode.h"

static const char *TAG = "rx_task";

void lora_rx_task(void *pvParameters) {
    uint8_t  raw[255];
    uint8_t  irq_signal;

    ESP_LOGI(TAG, "RX task started");

    while (1) {
        /* Wait for IRQ handler to signal an RxDone event */
        if (xQueueReceive(g_rx_ready_queue, &irq_signal, portMAX_DELAY) != pdTRUE)
            continue;

        /* Read received packet from SX1276 FIFO */
        uint8_t nb_bytes = sx1276_read_reg(REG_RX_NB_BYTES);
        if (nb_bytes == 0 || nb_bytes > (uint8_t)(sizeof(raw) - 1)) {
            ESP_LOGW(TAG, "RxDone but invalid byte count: %u", nb_bytes);
            sx1276_set_rx_mode();
            continue;
        }

        /* Point FIFO address pointer to start of received packet */
        uint8_t curr_addr = sx1276_read_reg(REG_FIFO_RX_CURR_ADDR);
        sx1276_write_reg(REG_FIFO_ADDR_PTR, curr_addr);
        sx1276_read_fifo(raw, nb_bytes);

        int16_t rssi = sx1276_get_rssi();
        int8_t  snr  = sx1276_get_snr();

        ESP_LOGI(TAG, "Packet received: %u bytes, RSSI=%ddBm, SNR=%ddB", nb_bytes, rssi, snr);

        /* Build event */
        analyzer_event_t evt = {
            .type   = EVT_RX_PACKET,
            .ts_ms  = (uint32_t)(esp_timer_get_time() / 1000),
        };
        evt.rx.rssi_dbm = rssi;
        evt.rx.snr_db   = snr;
        evt.rx.raw_len  = nb_bytes;
        evt.rx.decoded  = false;

        /* Try to decode as our channel */
        mesh_packet_t pkt;
        if (packet_decode(raw, nb_bytes, &pkt)) {
            evt.rx.src_addr  = pkt.src_addr;
            evt.rx.dst_addr  = pkt.dst_addr;
            evt.rx.packet_id = pkt.packet_id;
            evt.rx.flags     = (uint8_t)(pkt.hop_limit | (pkt.want_ack ? 0x08 : 0x00));
            evt.rx.decoded   = true;

            /* Try to parse inner Data protobuf */
            mesh_data_t data;
            if (proto_decode_data(pkt.payload, pkt.payload_len, &data)) {
                evt.rx.portnum = (uint8_t)data.portnum;
                /* Store inner payload so logger_task can decode per portnum */
                size_t copy = data.payload_len < sizeof(evt.rx.raw_payload)
                                ? data.payload_len : sizeof(evt.rx.raw_payload);
                memcpy(evt.rx.raw_payload, data.payload, copy);
                evt.rx.raw_len = (uint8_t)copy;
            } else {
                evt.rx.portnum = PORTNUM_UNKNOWN_APP;
                size_t copy = pkt.payload_len < sizeof(evt.rx.raw_payload)
                                ? pkt.payload_len : sizeof(evt.rx.raw_payload);
                memcpy(evt.rx.raw_payload, pkt.payload, copy);
                evt.rx.raw_len = (uint8_t)copy;
            }
        } else {
            /* Foreign channel or unrecognised packet */
            size_t copy = nb_bytes < sizeof(evt.rx.raw_payload)
                            ? nb_bytes : sizeof(evt.rx.raw_payload);
            memcpy(evt.rx.raw_payload, raw, copy);
        }

        /* Update shared stats */
        if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_stats.last_rx_rssi_dbm = rssi;
            g_stats.last_rx_snr_db   = snr;
            g_stats.rx_count++;
            xSemaphoreGive(g_stats_mutex);
        }

        xQueueSend(g_event_queue, &evt, pdMS_TO_TICKS(100));

        /* Return to RX continuous mode */
        sx1276_set_rx_mode();
    }
}
