#include <string.h>
#include "proto_encode.h"

/* ── Protobuf wire format helpers ───────────────────────────────────────── */

/* Wire types */
#define WT_VARINT  0
#define WT_LEN     2
#define WT_FIX32   5

static size_t write_varint(uint8_t *buf, size_t pos, size_t buf_size, uint64_t val) {
    do {
        if (pos >= buf_size) return 0;
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val) byte |= 0x80;
        buf[pos++] = byte;
    } while (val);
    return pos;
}

static size_t write_tag(uint8_t *buf, size_t pos, size_t buf_size,
                        uint32_t field_num, uint8_t wire_type) {
    return write_varint(buf, pos, buf_size, ((uint64_t)field_num << 3) | wire_type);
}

static size_t write_bytes(uint8_t *buf, size_t pos, size_t buf_size,
                          uint32_t field_num, const uint8_t *data, size_t data_len) {
    pos = write_tag(buf, pos, buf_size, field_num, WT_LEN);
    if (!pos) return 0;
    pos = write_varint(buf, pos, buf_size, (uint64_t)data_len);
    if (!pos) return 0;
    if (pos + data_len > buf_size) return 0;
    memcpy(buf + pos, data, data_len);
    return pos + data_len;
}

static size_t write_string(uint8_t *buf, size_t pos, size_t buf_size,
                           uint32_t field_num, const char *str) {
    return write_bytes(buf, pos, buf_size, field_num,
                       (const uint8_t *)str, strlen(str));
}

static size_t write_uint32(uint8_t *buf, size_t pos, size_t buf_size,
                           uint32_t field_num, uint32_t val) {
    if (val == 0) return pos;   /* skip default values */
    pos = write_tag(buf, pos, buf_size, field_num, WT_VARINT);
    if (!pos) return 0;
    return write_varint(buf, pos, buf_size, (uint64_t)val);
}

static size_t write_bool(uint8_t *buf, size_t pos, size_t buf_size,
                         uint32_t field_num, bool val) {
    if (!val) return pos;       /* skip false (default) */
    pos = write_tag(buf, pos, buf_size, field_num, WT_VARINT);
    if (!pos) return 0;
    return write_varint(buf, pos, buf_size, 1);
}

/* ── Varint / length-delimited decode helpers ───────────────────────────── */

static size_t read_fixed32(const uint8_t *buf, size_t pos, size_t len, uint32_t *out) {
    if (pos + 4 > len) return 0;
    *out = (uint32_t)buf[pos]
         | ((uint32_t)buf[pos+1] <<  8)
         | ((uint32_t)buf[pos+2] << 16)
         | ((uint32_t)buf[pos+3] << 24);
    return pos + 4;
}

static size_t read_varint(const uint8_t *buf, size_t pos, size_t len, uint64_t *out) {
    *out = 0;
    int shift = 0;
    do {
        if (pos >= len || shift >= 64) return 0;
        uint8_t b = buf[pos++];
        *out |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) return pos;
    } while (1);
}

/* ── Public encode functions ─────────────────────────────────────────────── */

size_t proto_encode_user(const mesh_user_t *user, uint8_t *buf, size_t buf_size) {
    size_t pos = 0;
    /* field 1: id (string) */
    pos = write_string(buf, pos, buf_size, 1, user->id);
    if (!pos && user->id[0]) return 0;
    /* field 2: long_name (string) */
    if (user->long_name[0])
        pos = write_string(buf, pos, buf_size, 2, user->long_name);
    /* field 3: short_name (string) */
    if (user->short_name[0])
        pos = write_string(buf, pos, buf_size, 3, user->short_name);
    /* field 4: macaddr (bytes, 6 bytes) */
    pos = write_bytes(buf, pos, buf_size, 4, user->macaddr, 6);
    /* field 6: hw_model (varint enum) */
    pos = write_uint32(buf, pos, buf_size, 6, user->hw_model);
    /* field 7: is_licensed (bool) */
    pos = write_bool(buf, pos, buf_size, 7, user->is_licensed);
    return pos;
}

size_t proto_encode_data(const mesh_data_t *data, uint8_t *buf, size_t buf_size) {
    size_t pos = 0;
    /* field 1: portnum (varint enum) */
    pos = write_uint32(buf, pos, buf_size, 1, (uint32_t)data->portnum);
    if (!pos && data->portnum) return 0;
    /* field 2: payload (bytes) */
    if (data->payload_len > 0)
        pos = write_bytes(buf, pos, buf_size, 2, data->payload, data->payload_len);
    /* field 3: want_response (bool) */
    pos = write_bool(buf, pos, buf_size, 3, data->want_response);
    return pos;
}

/* ── Public decode functions ─────────────────────────────────────────────── */

bool proto_decode_data(const uint8_t *buf, size_t len, mesh_data_t *out) {
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag_val;
        pos = read_varint(buf, pos, len, &tag_val);
        if (!pos) return false;
        uint32_t field_num = (uint32_t)(tag_val >> 3);
        uint8_t  wire_type = (uint8_t)(tag_val & 0x07);

        if (wire_type == WT_VARINT) {
            uint64_t val;
            pos = read_varint(buf, pos, len, &val);
            if (!pos) return false;
            if (field_num == 1) out->portnum       = (meshtastic_PortNum)val;
            if (field_num == 3) out->want_response  = (bool)val;
        } else if (wire_type == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(buf, pos, len, &dlen);
            if (!pos || pos + dlen > len) return false;
            if (field_num == 2) {
                size_t copy_len = dlen < sizeof(out->payload) ? (size_t)dlen : sizeof(out->payload);
                memcpy(out->payload, buf + pos, copy_len);
                out->payload_len = (uint16_t)copy_len;
            }
            pos += (size_t)dlen;
        } else if (wire_type == 1) {   /* int64/sfixed64: skip 8 bytes */
            if (pos + 8 > len) return false;
            pos += 8;
        } else if (wire_type == WT_FIX32) {  /* float/fixed32: skip 4 bytes */
            if (pos + 4 > len) return false;
            pos += 4;
        } else {
            return false;  /* wire types 3/4 (deprecated groups) = malformed */
        }
    }
    return true;
}

bool proto_decode_user(const uint8_t *buf, size_t len, mesh_user_t *out) {
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag_val;
        pos = read_varint(buf, pos, len, &tag_val);
        if (!pos) return false;
        uint32_t field_num = (uint32_t)(tag_val >> 3);
        uint8_t  wire_type = (uint8_t)(tag_val & 0x07);

        if (wire_type == WT_VARINT) {
            uint64_t val;
            pos = read_varint(buf, pos, len, &val);
            if (!pos) return false;
            if (field_num == 6) out->hw_model    = (uint32_t)val;
            if (field_num == 7) out->is_licensed = (bool)val;
        } else if (wire_type == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(buf, pos, len, &dlen);
            if (!pos || pos + dlen > len) return false;
            const uint8_t *src = buf + pos;
            if (field_num == 1) {
                size_t n = dlen < sizeof(out->id) - 1 ? (size_t)dlen : sizeof(out->id) - 1;
                memcpy(out->id, src, n);
            }
            if (field_num == 2) {
                size_t n = dlen < sizeof(out->long_name) - 1 ? (size_t)dlen : sizeof(out->long_name) - 1;
                memcpy(out->long_name, src, n);
            }
            if (field_num == 3) {
                size_t n = dlen < sizeof(out->short_name) - 1 ? (size_t)dlen : sizeof(out->short_name) - 1;
                memcpy(out->short_name, src, n);
            }
            if (field_num == 4 && dlen == 6) {
                memcpy(out->macaddr, src, 6);
            }
            pos += (size_t)dlen;
        } else {
            return false;
        }
    }
    return true;
}

const char *portnum_name(meshtastic_PortNum p) {
    switch (p) {
        case PORTNUM_TEXT_MESSAGE_APP: return "TEXT_MESSAGE_APP";
        case PORTNUM_REMOTE_HARDWARE:  return "REMOTE_HARDWARE_APP";
        case PORTNUM_POSITION_APP:     return "POSITION_APP";
        case PORTNUM_NODEINFO_APP:     return "NODEINFO_APP";
        case PORTNUM_ROUTING_APP:      return "ROUTING_APP";
        case PORTNUM_TELEMETRY_APP:    return "TELEMETRY_APP";
        default:                       return "UNKNOWN_APP";
    }
}

/* ── New portnum decoders ────────────────────────────────────────────────── */

bool proto_decode_position(const uint8_t *buf, size_t len, mesh_position_t *out) {
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag_val;
        pos = read_varint(buf, pos, len, &tag_val);
        if (!pos) return false;
        uint32_t fn = (uint32_t)(tag_val >> 3);
        uint8_t  wt = (uint8_t)(tag_val & 0x07);

        if (wt == WT_VARINT) {
            uint64_t val;
            pos = read_varint(buf, pos, len, &val);
            if (!pos) return false;
            if (fn == 3) out->altitude = (int32_t)(uint32_t)val;
            if (fn == 9) out->time     = (uint32_t)val;
        } else if (wt == WT_FIX32) {
            uint32_t val;
            pos = read_fixed32(buf, pos, len, &val);
            if (!pos) return false;
            if (fn == 1) out->latitude_i  = (int32_t)val;
            if (fn == 2) out->longitude_i = (int32_t)val;
        } else if (wt == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(buf, pos, len, &dlen);
            if (!pos || pos + dlen > len) return false;
            pos += (size_t)dlen;
        } else if (wt == 1) {
            if (pos + 8 > len) return false;
            pos += 8;
        } else {
            return false;
        }
    }
    return true;
}

bool proto_decode_routing(const uint8_t *buf, size_t len, mesh_routing_t *out) {
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag_val;
        pos = read_varint(buf, pos, len, &tag_val);
        if (!pos) return false;
        uint32_t fn = (uint32_t)(tag_val >> 3);
        uint8_t  wt = (uint8_t)(tag_val & 0x07);

        if (wt == WT_VARINT) {
            uint64_t val;
            pos = read_varint(buf, pos, len, &val);
            if (!pos) return false;
            if (fn == 3) out->error_reason = (uint32_t)val;
        } else if (wt == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(buf, pos, len, &dlen);
            if (!pos || pos + dlen > len) return false;
            pos += (size_t)dlen;
        } else if (wt == WT_FIX32) {
            if (pos + 4 > len) return false;
            pos += 4;
        } else if (wt == 1) {
            if (pos + 8 > len) return false;
            pos += 8;
        } else {
            return false;
        }
    }
    return true;
}

static void decode_device_metrics(const uint8_t *sub, size_t sub_len, mesh_telemetry_t *out) {
    size_t pos = 0;
    while (pos < sub_len) {
        uint64_t tag_val;
        pos = read_varint(sub, pos, sub_len, &tag_val);
        if (!pos) return;
        uint32_t fn = (uint32_t)(tag_val >> 3);
        uint8_t  wt = (uint8_t)(tag_val & 0x07);
        if (wt == WT_VARINT) {
            uint64_t val;
            pos = read_varint(sub, pos, sub_len, &val);
            if (!pos) return;
            if (fn == 1) out->battery_level = (uint32_t)val;
        } else if (wt == WT_FIX32) {
            uint32_t raw;
            pos = read_fixed32(sub, pos, sub_len, &raw);
            if (!pos) return;
            float f; memcpy(&f, &raw, 4);
            if (fn == 2) out->voltage = f;
            if (fn == 3) out->channel_utilization = f;
            if (fn == 4) out->air_util_tx = f;
        } else if (wt == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(sub, pos, sub_len, &dlen);
            if (!pos || pos + dlen > sub_len) return;
            pos += (size_t)dlen;
        } else { return; }
    }
}

static void decode_env_metrics(const uint8_t *sub, size_t sub_len, mesh_telemetry_t *out) {
    size_t pos = 0;
    while (pos < sub_len) {
        uint64_t tag_val;
        pos = read_varint(sub, pos, sub_len, &tag_val);
        if (!pos) return;
        uint32_t fn = (uint32_t)(tag_val >> 3);
        uint8_t  wt = (uint8_t)(tag_val & 0x07);
        if (wt == WT_FIX32) {
            uint32_t raw;
            pos = read_fixed32(sub, pos, sub_len, &raw);
            if (!pos) return;
            float f; memcpy(&f, &raw, 4);
            if (fn == 1) out->temperature = f;
            if (fn == 2) out->relative_humidity = f;
            if (fn == 3) out->barometric_pressure = f;
        } else if (wt == WT_VARINT) {
            uint64_t val;
            pos = read_varint(sub, pos, sub_len, &val);
            if (!pos) return;
        } else if (wt == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(sub, pos, sub_len, &dlen);
            if (!pos || pos + dlen > sub_len) return;
            pos += (size_t)dlen;
        } else { return; }
    }
}

bool proto_decode_telemetry(const uint8_t *buf, size_t len, mesh_telemetry_t *out) {
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag_val;
        pos = read_varint(buf, pos, len, &tag_val);
        if (!pos) return false;
        uint32_t fn = (uint32_t)(tag_val >> 3);
        uint8_t  wt = (uint8_t)(tag_val & 0x07);

        if (wt == WT_VARINT) {
            uint64_t val;
            pos = read_varint(buf, pos, len, &val);
            if (!pos) return false;
            if (fn == 1) out->time = (uint32_t)val;
        } else if (wt == WT_LEN) {
            uint64_t dlen;
            pos = read_varint(buf, pos, len, &dlen);
            if (!pos || pos + dlen > len) return false;
            if (fn == 2) { out->has_device = true; decode_device_metrics(buf + pos, (size_t)dlen, out); }
            if (fn == 3) { out->has_env    = true; decode_env_metrics(buf + pos, (size_t)dlen, out); }
            pos += (size_t)dlen;
        } else if (wt == WT_FIX32) {
            if (pos + 4 > len) return false;
            pos += 4;
        } else if (wt == 1) {
            if (pos + 8 > len) return false;
            pos += 8;
        } else {
            return false;
        }
    }
    return true;
}
