#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "events.h"
#include "../meshtastic/proto/proto_encode.h"
#include "bt_spp.h"

static const char *TAG = "logger";

/* ── Static output buffers (avoids stack pressure in logger_task) ────────── */

static char s_json[1024];
static char s_payload[640];
static char s_esc[512];   /* scratch for json_esc() */

/* ── NDJSON output ───────────────────────────────────────────────────────── */

static void output_line(const char *line) {
    printf("%s\n", line);
    size_t len = strlen(line);
    if (len > 0) {
        bt_spp_write((const uint8_t *)line, (uint16_t)(len > 65535u ? 65535u : len));
        bt_spp_write((const uint8_t *)"\n", 1);
    }
}

/* ── JSON string escaping ────────────────────────────────────────────────── */

/* Write JSON-escaped content of in[0..in_len) into out[0..out_sz), NUL-terminated. */
static size_t json_esc(char *out, size_t out_sz, const char *in, size_t in_len) {
    size_t pos = 0;
    if (out_sz == 0) return 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0) break;
        size_t avail = out_sz - pos - 1;  /* reserve 1 for NUL */
        if (c == '"' || c == '\\') {
            if (avail < 2) break;
            out[pos++] = '\\'; out[pos++] = (char)c;
        } else if (c == '\n') {
            if (avail < 2) break;
            out[pos++] = '\\'; out[pos++] = 'n';
        } else if (c == '\r') {
            if (avail < 2) break;
            out[pos++] = '\\'; out[pos++] = 'r';
        } else if (c == '\t') {
            if (avail < 2) break;
            out[pos++] = '\\'; out[pos++] = 't';
        } else if (c < 0x20) {
            if (avail < 6) break;
            int w = snprintf(out + pos, avail + 1, "\\u%04x", c);
            if (w > 0) pos += (size_t)w;
        } else {
            if (avail < 1) break;
            out[pos++] = (char)c;
        }
    }
    out[pos] = '\0';
    return pos;
}

/* ── Lookup tables ───────────────────────────────────────────────────────── */

static const char *hw_model_name(uint32_t m) {
    switch (m) {
        case  0: return "UNSET";
        case  1: return "TBEAM";
        case  4: return "TBEAM_V0_7";
        case  6: return "T_ECHO";
        case  9: return "RAK4631";
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
        case  0: return "NONE";
        case  1: return "NO_ROUTE";
        case  2: return "GOT_NAK";
        case  3: return "TIMEOUT";
        case  4: return "NO_INTERFACE";
        case  5: return "MAX_RETRANSMIT";
        case  6: return "NO_CHANNEL";
        case  7: return "TOO_LARGE";
        case  8: return "NO_RESPONSE";
        case  9: return "PKT_TOO_GOOD";
        case 10: return "DUTY_CYCLE_LIMIT";
        case 32: return "BAD_REQUEST";
        case 33: return "NOT_AUTHORIZED";
        default: return "UNKNOWN_ERROR";
    }
}

/* ── Per-portnum payload JSON builders ───────────────────────────────────── */

static void build_payload_json(char *buf, size_t n, const rx_packet_t *rx) {
    const uint8_t *pl  = rx->raw_payload;
    size_t         pll = rx->raw_len;

    switch ((meshtastic_PortNum)rx->portnum) {

    case PORTNUM_TEXT_MESSAGE_APP: {
        json_esc(s_esc, sizeof(s_esc), (const char *)pl, pll);
        snprintf(buf, n, "{\"text\":\"%s\"}", s_esc);
        break;
    }

    case PORTNUM_NODEINFO_APP: {
        mesh_user_t u;
        if (!proto_decode_user(pl, pll, &u)) {
            snprintf(buf, n, "{\"raw_len\":%u}", (unsigned)pll);
            break;
        }
        char esc_id[24], esc_long[96], esc_short[20];
        json_esc(esc_id,    sizeof(esc_id),    u.id,         strlen(u.id));
        json_esc(esc_long,  sizeof(esc_long),  u.long_name,  strlen(u.long_name));
        json_esc(esc_short, sizeof(esc_short), u.short_name, strlen(u.short_name));
        snprintf(buf, n,
                 "{\"id\":\"%s\",\"long_name\":\"%s\",\"short_name\":\"%s\""
                 ",\"hw_model\":%" PRIu32 ",\"hw_model_name\":\"%s\""
                 ",\"macaddr\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"licensed\":%s}",
                 esc_id, esc_long, esc_short,
                 u.hw_model, hw_model_name(u.hw_model),
                 u.macaddr[0], u.macaddr[1], u.macaddr[2],
                 u.macaddr[3], u.macaddr[4], u.macaddr[5],
                 u.is_licensed ? "true" : "false");
        break;
    }

    case PORTNUM_POSITION_APP: {
        mesh_position_t pos;
        if (!proto_decode_position(pl, pll, &pos)) {
            snprintf(buf, n, "{\"raw_len\":%u}", (unsigned)pll);
            break;
        }
        double lat = pos.latitude_i  / 1e7;
        double lon = pos.longitude_i / 1e7;
        if (pos.time)
            snprintf(buf, n, "{\"lat\":%.7f,\"lon\":%.7f,\"alt\":%d,\"gps_time\":%" PRIu32 "}",
                     lat, lon, (int)pos.altitude, pos.time);
        else
            snprintf(buf, n, "{\"lat\":%.7f,\"lon\":%.7f,\"alt\":%d}",
                     lat, lon, (int)pos.altitude);
        break;
    }

    case PORTNUM_ROUTING_APP: {
        mesh_routing_t r;
        if (!proto_decode_routing(pl, pll, &r)) {
            snprintf(buf, n, "{\"raw_len\":%u}", (unsigned)pll);
            break;
        }
        const char *ename = route_error_name(r.error_reason);
        snprintf(buf, n, "{\"error_code\":%" PRIu32 ",\"error_name\":\"%s\"}",
                 r.error_reason, ename);
        break;
    }

    case PORTNUM_TELEMETRY_APP: {
        mesh_telemetry_t t;
        if (!proto_decode_telemetry(pl, pll, &t)) {
            snprintf(buf, n, "{\"raw_len\":%u}", (unsigned)pll);
            break;
        }
        if (t.has_device && t.has_env) {
            snprintf(buf, n,
                     "{\"battery_level\":%" PRIu32 ",\"voltage\":%.2f"
                     ",\"channel_utilization\":%.1f,\"air_util_tx\":%.1f"
                     ",\"temperature\":%.1f,\"relative_humidity\":%.1f"
                     ",\"barometric_pressure\":%.1f}",
                     t.battery_level, (double)t.voltage,
                     (double)t.channel_utilization, (double)t.air_util_tx,
                     (double)t.temperature, (double)t.relative_humidity,
                     (double)t.barometric_pressure);
        } else if (t.has_device) {
            snprintf(buf, n,
                     "{\"battery_level\":%" PRIu32 ",\"voltage\":%.2f"
                     ",\"channel_utilization\":%.1f,\"air_util_tx\":%.1f}",
                     t.battery_level, (double)t.voltage,
                     (double)t.channel_utilization, (double)t.air_util_tx);
        } else if (t.has_env) {
            snprintf(buf, n,
                     "{\"temperature\":%.1f,\"relative_humidity\":%.1f"
                     ",\"barometric_pressure\":%.1f}",
                     (double)t.temperature, (double)t.relative_humidity,
                     (double)t.barometric_pressure);
        } else {
            snprintf(buf, n, "{\"raw_len\":%u}", (unsigned)pll);
        }
        break;
    }

    default: {
        size_t pos = 0;
        pos += (size_t)snprintf(buf + pos, n - pos, "{\"hex\":\"");
        for (size_t i = 0; i < pll && pos + 16 < n; i++)
            pos += (size_t)snprintf(buf + pos, n - pos, "%02x", pl[i]);
        snprintf(buf + pos, n - pos, "\",\"len\":%u}", (unsigned)pll);
        break;
    }
    }
}

/* ── Display packet summary (for OLED — unchanged) ───────────────────────── */

static void build_disp_summary(const rx_packet_t *rx, char *buf, size_t buf_len) {
    const uint8_t *pl  = rx->raw_payload;
    size_t         pll = rx->raw_len;

    switch ((meshtastic_PortNum)rx->portnum) {
    case PORTNUM_TEXT_MESSAGE_APP: {
        size_t n = pll < 15 ? pll : 15;
        snprintf(buf, buf_len, "TXT:\"%.*s\"", (int)n, (const char *)pl);
        break;
    }
    case PORTNUM_NODEINFO_APP: {
        mesh_user_t u;
        if (proto_decode_user(pl, pll, &u) && u.long_name[0])
            snprintf(buf, buf_len, "NFO:%.16s", u.long_name);
        else
            snprintf(buf, buf_len, "NFO:!%08" PRIx32, rx->src_addr);
        break;
    }
    case PORTNUM_POSITION_APP: {
        mesh_position_t pos;
        if (proto_decode_position(pl, pll, &pos) && (pos.latitude_i || pos.longitude_i)) {
            double lat = pos.latitude_i / 1e7;
            double lon = pos.longitude_i / 1e7;
            snprintf(buf, buf_len, "POS:%.4f,%.4f", lat, lon);
        } else {
            snprintf(buf, buf_len, "POS:%uB", (unsigned)pll);
        }
        break;
    }
    case PORTNUM_ROUTING_APP: {
        mesh_routing_t r;
        if (proto_decode_routing(pl, pll, &r))
            snprintf(buf, buf_len, "RTG:%.15s", route_error_name(r.error_reason));
        else
            snprintf(buf, buf_len, "RTG:%uB", (unsigned)pll);
        break;
    }
    case PORTNUM_TELEMETRY_APP: {
        mesh_telemetry_t t;
        if (proto_decode_telemetry(pl, pll, &t)) {
            if (t.has_device)
                snprintf(buf, buf_len, "TEL:%u%% %.2fV",
                         (unsigned)t.battery_level, (double)t.voltage);
            else if (t.has_env)
                snprintf(buf, buf_len, "TEL:%.1fC %.0f%%RH",
                         (double)t.temperature, (double)t.relative_humidity);
            else
                snprintf(buf, buf_len, "TEL:%uB", (unsigned)pll);
        } else {
            snprintf(buf, buf_len, "TEL:%uB", (unsigned)pll);
        }
        break;
    }
    default:
        snprintf(buf, buf_len, "%.4s:%uB",
                 portnum_name((meshtastic_PortNum)rx->portnum), (unsigned)pll);
        break;
    }
}

static void update_disp_pkt(const rx_packet_t *rx) {
    disp_pkt_t p = {
        .src_addr = rx->src_addr,
        .rssi_dbm = rx->rssi_dbm,
        .snr_db   = rx->snr_db,
        .valid    = true,
    };
    build_disp_summary(rx, p.summary, sizeof(p.summary));

    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_stats.disp_pkts[1] = g_stats.disp_pkts[0];
        g_stats.disp_pkts[0] = p;
        xSemaphoreGive(g_stats_mutex);
    }
}

/* ── Event serialisers ───────────────────────────────────────────────────── */

static void log_rx_packet(const analyzer_event_t *evt) {
    const rx_packet_t *rx = &evt->rx;

    if (!rx->decoded) {
        snprintf(s_json, sizeof(s_json),
                 "{\"type\":\"rx\",\"ts\":%" PRIu32
                 ",\"decoded\":false,\"rssi\":%d,\"snr\":%d,\"raw_len\":%u}",
                 evt->ts_ms, rx->rssi_dbm, rx->snr_db, (unsigned)rx->raw_len);
        output_line(s_json);
        return;
    }

    build_payload_json(s_payload, sizeof(s_payload), rx);

    uint8_t hop_limit = rx->flags & 0x07;
    uint8_t hop_start = (rx->flags >> 5) & 0x07;
    bool    want_ack  = (rx->flags >> 3) & 0x01;

    snprintf(s_json, sizeof(s_json),
             "{\"type\":\"rx\",\"ts\":%" PRIu32
             ",\"src\":\"!%08" PRIx32 "\",\"dst\":\"!%08" PRIx32 "\""
             ",\"pkt\":%" PRIu32
             ",\"hop_limit\":%u,\"hop_start\":%u,\"want_ack\":%s"
             ",\"relay_node\":%u"
             ",\"rssi\":%d,\"snr\":%d"
             ",\"decoded\":true"
             ",\"portnum\":%u,\"portnum_name\":\"%s\""
             ",\"payload\":%s}",
             evt->ts_ms,
             rx->src_addr, rx->dst_addr,
             rx->packet_id,
             (unsigned)hop_limit, (unsigned)hop_start,
             want_ack ? "true" : "false",
             (unsigned)rx->relay_node,
             rx->rssi_dbm, rx->snr_db,
             (unsigned)rx->portnum,
             portnum_name((meshtastic_PortNum)rx->portnum),
             s_payload);
    output_line(s_json);

    update_disp_pkt(rx);
}

static void log_noise_sample(const analyzer_event_t *evt) {
    snprintf(s_json, sizeof(s_json),
             "{\"type\":\"noise\",\"ts\":%" PRIu32
             ",\"floor_dbm\":%d,\"samples\":%u}",
             evt->ts_ms, evt->noise.noise_floor_dbm,
             (unsigned)evt->noise.sample_count);
    output_line(s_json);
}

static void log_tx_done(const analyzer_event_t *evt) {
    snprintf(s_json, sizeof(s_json),
             "{\"type\":\"tx_done\",\"ts\":%" PRIu32 "}", evt->ts_ms);
    output_line(s_json);
}

/* ── Task entry ─────────────────────────────────────────────────────────── */

void logger_task(void *pvParameters) {
    analyzer_event_t evt;
    ESP_LOGI(TAG, "Logger task started (NDJSON output)");

    while (1) {
        if (xQueueReceive(g_event_queue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt.type) {
            case EVT_RX_PACKET:    log_rx_packet(&evt);    break;
            case EVT_NOISE_SAMPLE: log_noise_sample(&evt); break;
            case EVT_TX_DONE:      log_tx_done(&evt);      break;
            case EVT_ERROR:
                snprintf(s_json, sizeof(s_json),
                         "{\"type\":\"error\",\"ts\":%" PRIu32 "}", evt.ts_ms);
                output_line(s_json);
                break;
        }
    }
}
