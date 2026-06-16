#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "events.h"
#include "../meshtastic/packet.h"

static const char *TAG = "cmd";

/* ── Minimal JSON field extractor ────────────────────────────────────────── */

/* Find "key":value in json and parse value as double. Returns false if absent. */
static bool json_get_double(const char *json, const char *key, double *out) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    char *endp;
    double v = strtod(p, &endp);
    if (endp == p) return false;
    *out = v;
    return true;
}

/* ── Command handlers ────────────────────────────────────────────────────── */

static void handle_send_position(const char *json) {
    double lat = 0, lon = 0, alt = 0;

    if (!json_get_double(json, "lat", &lat) ||
        !json_get_double(json, "lon", &lon)) {
        ESP_LOGW(TAG, "send_position: missing lat or lon");
        return;
    }
    json_get_double(json, "alt", &alt);  /* optional */

    int32_t lat_i = (int32_t)(lat * 1e7);
    int32_t lon_i = (int32_t)(lon * 1e7);
    int32_t alt_m = (int32_t)alt;

    ESP_LOGI(TAG, "Position cmd: lat=%.7f lon=%.7f alt=%d m → lat_i=%ld lon_i=%ld",
             lat, lon, (int)alt_m, (long)lat_i, (long)lon_i);

    uint8_t pkt_buf[255];
    size_t  pkt_len = packet_build_position(g_node_addr, lat_i, lon_i, alt_m, pkt_buf);
    if (!pkt_len) {
        ESP_LOGE(TAG, "Failed to build position packet");
        return;
    }

    lora_tx_pkt_t req;
    req.len = (uint8_t)pkt_len;
    memcpy(req.data, pkt_buf, pkt_len);

    if (xQueueSend(g_tx_request_queue, &req, pdMS_TO_TICKS(500)) != pdTRUE)
        ESP_LOGW(TAG, "TX queue full — position packet dropped");
    else
        ESP_LOGI(TAG, "Position packet queued (%zu bytes)", pkt_len);
}

/* ── Command dispatcher (also called from bt_spp DATA_IND handler) ───────── */

void cmd_process_line(const char *line) {
    if (!line || line[0] != '{') return;

    if (strstr(line, "\"send_position\""))
        handle_send_position(line);
    else
        ESP_LOGW(TAG, "Unknown cmd: %.60s", line);
}

/* ── Task entry ──────────────────────────────────────────────────────────── */

void cmd_task(void *pvParameters) {
    static char line[320];
    ESP_LOGI(TAG, "Command task started — reading JSON commands from UART");

    while (1) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* Strip trailing whitespace / line endings */
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' '))
            line[--n] = '\0';

        cmd_process_line(line);
    }
}
