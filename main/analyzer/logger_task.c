#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "events.h"
#include "../meshtastic/proto/proto_encode.h"

static const char *TAG = "logger";

static void log_rx_packet(const analyzer_event_t *evt) {
    const rx_packet_t *rx = &evt->rx;
    if (rx->decoded) {
        printf("{\"event\":\"rx_packet\","
               "\"ts_ms\":%" PRIu32 ","
               "\"rssi_dbm\":%d,"
               "\"snr_db\":%d,"
               "\"src_addr\":\"0x%08" PRIX32 "\","
               "\"dst_addr\":\"0x%08" PRIX32 "\","
               "\"packet_id\":%" PRIu32 ","
               "\"portnum\":\"%s\","
               "\"decoded\":true,"
               "\"payload_summary\":\"%s\"}\n",
               evt->ts_ms,
               rx->rssi_dbm, rx->snr_db,
               rx->src_addr, rx->dst_addr, rx->packet_id,
               portnum_name((meshtastic_PortNum)rx->portnum),
               rx->payload_summary);
    } else {
        printf("{\"event\":\"rx_packet\","
               "\"ts_ms\":%" PRIu32 ","
               "\"rssi_dbm\":%d,"
               "\"snr_db\":%d,"
               "\"src_addr\":\"unknown\","
               "\"decoded\":false,"
               "\"raw_len\":%u}\n",
               evt->ts_ms, rx->rssi_dbm, rx->snr_db, rx->raw_len);
    }
}

static void log_noise_sample(const analyzer_event_t *evt) {
    printf("{\"event\":\"noise_sample\","
           "\"ts_ms\":%" PRIu32 ","
           "\"noise_floor_dbm\":%d,"
           "\"sample_count\":%u}\n",
           evt->ts_ms,
           evt->noise.noise_floor_dbm,
           evt->noise.sample_count);
}

static void log_tx_done(const analyzer_event_t *evt) {
    printf("{\"event\":\"tx_done\","
           "\"ts_ms\":%" PRIu32 "}\n",
           evt->ts_ms);
}

void logger_task(void *pvParameters) {
    analyzer_event_t evt;
    ESP_LOGI(TAG, "Logger task started — JSON output at %d baud", 115200);

    while (1) {
        if (xQueueReceive(g_event_queue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt.type) {
            case EVT_RX_PACKET:    log_rx_packet(&evt);    break;
            case EVT_NOISE_SAMPLE: log_noise_sample(&evt); break;
            case EVT_TX_DONE:      log_tx_done(&evt);      break;
            case EVT_ERROR:
                printf("{\"event\":\"error\",\"ts_ms\":%" PRIu32 "}\n", evt.ts_ms);
                break;
        }
    }
}
