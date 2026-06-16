#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "packet.h"
#include "crypto.h"
#include "proto/proto_encode.h"
#include "../config.h"

uint32_t g_tx_packet_id = 1;

size_t packet_build_position(uint32_t node_addr, int32_t lat_i, int32_t lon_i,
                             int32_t alt_m, uint8_t *out_buf) {
    /* 1. Build Position message */
    mesh_position_t pos = {
        .latitude_i  = lat_i,
        .longitude_i = lon_i,
        .altitude    = alt_m,
        .time        = 0,   /* no GPS time source — leave unset */
    };

    uint8_t pos_buf[32];
    size_t  pos_len = proto_encode_position(&pos, pos_buf, sizeof(pos_buf));
    if (!pos_len) return 0;

    /* 2. Wrap in Data message */
    mesh_data_t data = {0};
    data.portnum     = PORTNUM_POSITION_APP;
    data.payload_len = (uint16_t)pos_len;
    memcpy(data.payload, pos_buf, pos_len);

    uint8_t data_buf[64];
    size_t  data_len = proto_encode_data(&data, data_buf, sizeof(data_buf));
    if (!data_len) return 0;

    /* 3. Encrypt */
    uint32_t pkt_id = g_tx_packet_id++;
    crypto_ctr(data_buf, data_len, pkt_id, node_addr);

    /* 4. Prepend header */
    mesh_header_t hdr = {
        .dst_addr   = 0xFFFFFFFF,
        .src_addr   = node_addr,
        .packet_id  = pkt_id,
        .flags      = (uint8_t)((3 & 0x07) | ((3 & 0x07) << 5)),
        .channel    = crypto_get_channel_hash(),
        .next_hop   = 0,
        .relay_node = 0,
    };
    memcpy(out_buf, &hdr, MESH_HEADER_LEN);
    memcpy(out_buf + MESH_HEADER_LEN, data_buf, data_len);

    return MESH_HEADER_LEN + data_len;
}

size_t packet_build_nodeinfo(uint32_t node_addr, const uint8_t mac[6],
                             uint8_t *out_buf) {
    /* 1. Build User message */
    mesh_user_t user = {0};
    snprintf(user.id, sizeof(user.id), "!%08" PRIx32, node_addr);
    snprintf(user.long_name,  sizeof(user.long_name),  "TLoRA-%04" PRIx32, (node_addr & 0xFFFF));
    snprintf(user.short_name, sizeof(user.short_name), "%04" PRIx32, (node_addr & 0xFFFF));
    memcpy(user.macaddr, mac, 6);
    user.hw_model    = 37;   /* TLORA_V1 hardware model ID in Meshtastic */
    user.is_licensed = false;

    uint8_t user_buf[128];
    size_t  user_len = proto_encode_user(&user, user_buf, sizeof(user_buf));
    if (!user_len) return 0;

    /* 2. Build Data message */
    mesh_data_t data = {0};
    data.portnum      = PORTNUM_NODEINFO_APP;
    data.payload_len  = (uint16_t)user_len;
    memcpy(data.payload, user_buf, user_len);
    data.want_response = false;

    uint8_t data_buf[200];
    size_t  data_len = proto_encode_data(&data, data_buf, sizeof(data_buf));
    if (!data_len) return 0;

    /* 3. Encrypt payload using CTR with our packet_id and src_addr */
    uint32_t pkt_id = g_tx_packet_id++;
    crypto_ctr(data_buf, data_len, pkt_id, node_addr);

    /* 4. Build OTA packet header */
    mesh_header_t hdr = {
        .dst_addr   = 0xFFFFFFFF,    /* broadcast */
        .src_addr   = node_addr,
        .packet_id  = pkt_id,
        /* flags: hop_limit=3, want_ack=0, via_mqtt=0, hop_start=3 (Meshtastic 2.x format) */
        .flags      = (uint8_t)((3 & 0x07) | ((3 & 0x07) << 5)),
        .channel    = crypto_get_channel_hash(),
        .next_hop   = 0,
        .relay_node = 0,
    };
    memcpy(out_buf, &hdr, MESH_HEADER_LEN);
    memcpy(out_buf + MESH_HEADER_LEN, data_buf, data_len);

    return MESH_HEADER_LEN + data_len;
}

bool packet_decode(const uint8_t *raw, uint8_t raw_len, mesh_packet_t *out) {
    if (raw_len < MESH_HEADER_LEN) return false;

    const mesh_header_t *hdr = (const mesh_header_t *)raw;
    out->dst_addr  = hdr->dst_addr;
    out->src_addr  = hdr->src_addr;
    out->packet_id = hdr->packet_id;
    out->hop_limit  = hdr->flags & 0x07;
    out->want_ack   = (hdr->flags >> 3) & 0x01;
    out->hop_start  = (hdr->flags >> 5) & 0x07;
    out->relay_node = hdr->relay_node;
    /* Channel validation is implicit: wrong-channel AES-CTR output fails protobuf parsing. */

    out->payload_len = raw_len - MESH_HEADER_LEN;
    memcpy(out->payload, raw + MESH_HEADER_LEN, out->payload_len);

    /* Decrypt in-place */
    crypto_ctr(out->payload, out->payload_len, hdr->packet_id, hdr->src_addr);

    return true;
}
