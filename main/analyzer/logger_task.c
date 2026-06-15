#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "events.h"
#include "../meshtastic/proto/proto_encode.h"

static const char *TAG = "logger";

/* ── Formatting helpers ─────────────────────────────────────────────────── */

static void fmt_ts(char *buf, size_t n, uint32_t ts_ms) {
    snprintf(buf, n, "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ".%03" PRIu32,
             ts_ms / 3600000,
             (ts_ms / 60000) % 60,
             (ts_ms / 1000)  % 60,
             ts_ms % 1000);
}

static void fmt_addr(char *buf, size_t n, uint32_t addr) {
    if (addr == 0xFFFFFFFF)
        snprintf(buf, n, "BROADCAST");
    else
        snprintf(buf, n, "!%08" PRIx32, addr);
}

static const char *hw_model_name(uint32_t m) {
    switch (m) {
        case 0:  return "UNSET";
        case 1:  return "TBEAM";
        case 4:  return "TBEAM_V0_7";
        case 6:  return "T_ECHO";
        case 9:  return "RAK4631";
        case 10: return "HELTEC_V2_1";
        case 11: return "HELTEC_V2_0";
        case 14: return "RAK11200";
        case 19: return "NANO_G1";
        case 24: return "HELTEC_V3";
        case 37: return "TLORA_V1";
        case 38: return "TLORA_V2";
        case 39: return "TLORA_V2_1_16";
        case 40: return "TLORA_T3_S3";
        default: return "HW_UNKNOWN";
    }
}

static const char *route_error_name(uint32_t e) {
    switch (e) {
        case 0:  return "NONE (ACK)";
        case 1:  return "NO_ROUTE";
        case 2:  return "GOT_NAK";
        case 3:  return "TIMEOUT";
        case 4:  return "NO_INTERFACE";
        case 5:  return "MAX_RETRANSMIT";
        case 6:  return "NO_CHANNEL";
        case 7:  return "TOO_LARGE";
        case 8:  return "NO_RESPONSE";
        case 9:  return "PKT_TOO_GOOD";
        case 10: return "DUTY_CYCLE_LIMIT";
        case 32: return "BAD_REQUEST";
        case 33: return "NOT_AUTHORIZED";
        default: return "UNKNOWN_ERROR";
    }
}

/* ── Per-portnum display ─────────────────────────────────────────────────── */

static void print_text_message(const uint8_t *pl, size_t pl_len) {
    char text[241];
    size_t n = pl_len < sizeof(text) - 1 ? pl_len : sizeof(text) - 1;
    memcpy(text, pl, n);
    text[n] = '\0';
    printf("    TEXT_MESSAGE: \"%s\"\n", text);
}

static void print_position(const uint8_t *pl, size_t pl_len) {
    mesh_position_t pos;
    if (!proto_decode_position(pl, pl_len, &pos)) {
        printf("    POSITION: (parse error, %u bytes)\n", (unsigned)pl_len);
        return;
    }
    double lat = pos.latitude_i  / 1e7;
    double lon = pos.longitude_i / 1e7;
    char lat_c = lat >= 0 ? 'N' : 'S';
    char lon_c = lon >= 0 ? 'E' : 'W';
    if (lat < 0) lat = -lat;
    if (lon < 0) lon = -lon;
    printf("    POSITION: %.7f %c  %.7f %c  alt: %d m\n",
           lat, lat_c, lon, lon_c, (int)pos.altitude);
    if (pos.time) {
        struct tm utc;
        time_t t = (time_t)pos.time;
        gmtime_r(&t, &utc);
        printf("              GPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
               utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
               utc.tm_hour, utc.tm_min, utc.tm_sec);
    }
}

static void print_nodeinfo(const uint8_t *pl, size_t pl_len) {
    mesh_user_t user;
    if (!proto_decode_user(pl, pl_len, &user)) {
        printf("    NODEINFO: (parse error, %u bytes)\n", (unsigned)pl_len);
        return;
    }
    printf("    NODEINFO: \"%s\" (%s)  short: \"%s\"\n",
           user.long_name, user.id, user.short_name);
    printf("              hw: %-14s  MAC: %02X:%02X:%02X:%02X:%02X:%02X%s\n",
           hw_model_name(user.hw_model),
           user.macaddr[0], user.macaddr[1], user.macaddr[2],
           user.macaddr[3], user.macaddr[4], user.macaddr[5],
           user.is_licensed ? "  [licensed]" : "");
}

static void print_routing(const uint8_t *pl, size_t pl_len) {
    mesh_routing_t routing;
    if (!proto_decode_routing(pl, pl_len, &routing)) {
        printf("    ROUTING: (parse error, %u bytes)\n", (unsigned)pl_len);
        return;
    }
    printf("    ROUTING: error=%s\n", route_error_name(routing.error_reason));
}

static void print_telemetry(const uint8_t *pl, size_t pl_len) {
    mesh_telemetry_t telem;
    if (!proto_decode_telemetry(pl, pl_len, &telem)) {
        printf("    TELEMETRY: (parse error, %u bytes)\n", (unsigned)pl_len);
        return;
    }
    if (telem.has_device)
        printf("    TELEMETRY (device): batt %u%%  %.2f V  ch-util %.1f%%  air-tx %.1f%%\n",
               (unsigned)telem.battery_level, telem.voltage,
               telem.channel_utilization, telem.air_util_tx);
    if (telem.has_env)
        printf("    TELEMETRY (env):    temp %.1f C  humidity %.1f%%  pressure %.1f hPa\n",
               telem.temperature, telem.relative_humidity, telem.barometric_pressure);
    if (!telem.has_device && !telem.has_env)
        printf("    TELEMETRY: %u bytes (no device/env metrics decoded)\n", (unsigned)pl_len);
}

/* ── Event log functions ─────────────────────────────────────────────────── */

static void log_rx_packet(const analyzer_event_t *evt) {
    const rx_packet_t *rx = &evt->rx;
    char ts[16];
    fmt_ts(ts, sizeof(ts), evt->ts_ms);

    if (!rx->decoded) {
        printf("\n--- RX  %s  [FOREIGN CHANNEL]  ----------------------------------\n", ts);
        printf("    RF: RSSI %d dBm  SNR %d dB   raw: %u bytes\n",
               rx->rssi_dbm, rx->snr_db, rx->raw_len);
        printf("------------------------------------------------------------------------\n");
        return;
    }

    char src[12], dst[12];
    fmt_addr(src, sizeof(src), rx->src_addr);
    fmt_addr(dst, sizeof(dst), rx->dst_addr);
    uint8_t hops      = rx->flags & 0x07;
    uint8_t hop_start = (rx->flags >> 5) & 0x07;
    bool    want_ack  = (rx->flags >> 3) & 0x01;
    /* "hops: 3/3" = direct; "hops: 2/3" = forwarded once */
    char hops_buf[16];
    if (hop_start > 0 && hop_start != hops)
        snprintf(hops_buf, sizeof(hops_buf), "%u/%u", hops, hop_start);
    else
        snprintf(hops_buf, sizeof(hops_buf), "%u", hop_start ? hop_start : hops);

    printf("\n--- RX  %s  ----------------------------------------\n", ts);
    if (rx->relay_node)
        printf("    src: %-12s  dst: %-12s  pkt: 0x%08" PRIx32 "  hops: %s  via !xx%02x%s\n",
               src, dst, rx->packet_id, hops_buf, rx->relay_node, want_ack ? "  [ACK]" : "");
    else
        printf("    src: %-12s  dst: %-12s  pkt: 0x%08" PRIx32 "  hops: %s%s\n",
               src, dst, rx->packet_id, hops_buf, want_ack ? "  [ACK]" : "");
    printf("    RF:  RSSI %d dBm  SNR %d dB\n", rx->rssi_dbm, rx->snr_db);
    printf("    ---\n");

    const uint8_t *pl     = rx->raw_payload;
    size_t         pl_len = rx->raw_len;

    switch ((meshtastic_PortNum)rx->portnum) {
        case PORTNUM_TEXT_MESSAGE_APP: print_text_message(pl, pl_len); break;
        case PORTNUM_POSITION_APP:     print_position(pl, pl_len);     break;
        case PORTNUM_NODEINFO_APP:     print_nodeinfo(pl, pl_len);     break;
        case PORTNUM_ROUTING_APP:      print_routing(pl, pl_len);      break;
        case PORTNUM_TELEMETRY_APP:    print_telemetry(pl, pl_len);    break;
        default:
            printf("    %s: %u bytes\n",
                   portnum_name((meshtastic_PortNum)rx->portnum), (unsigned)pl_len);
            break;
    }

    printf("------------------------------------------------------------------------\n");
}

static void log_noise_sample(const analyzer_event_t *evt) {
    char ts[16];
    fmt_ts(ts, sizeof(ts), evt->ts_ms);
    printf("--- noise  %s   floor: %d dBm  (%u samples)\n",
           ts, evt->noise.noise_floor_dbm, evt->noise.sample_count);
}

static void log_tx_done(const analyzer_event_t *evt) {
    char ts[16];
    fmt_ts(ts, sizeof(ts), evt->ts_ms);
    printf("--- TX done  %s\n", ts);
}

/* ── Task entry ─────────────────────────────────────────────────────────── */

void logger_task(void *pvParameters) {
    analyzer_event_t evt;
    ESP_LOGI(TAG, "Logger task started");

    while (1) {
        if (xQueueReceive(g_event_queue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt.type) {
            case EVT_RX_PACKET:    log_rx_packet(&evt);    break;
            case EVT_NOISE_SAMPLE: log_noise_sample(&evt); break;
            case EVT_TX_DONE:      log_tx_done(&evt);      break;
            case EVT_ERROR:
                printf("--- ERROR  ts=%" PRIu32 "ms\n", evt.ts_ms);
                break;
        }
    }
}
