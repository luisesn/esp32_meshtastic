#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MESH_HEADER_LEN     16      /* 13 base + channel (2.3+) + next_hop + relay_node (2.5+) */
#define MAX_LORA_PAYLOAD    255

/* Raw OTA packet header (Meshtastic 2.5+) */
typedef struct __attribute__((packed)) {
    uint32_t dst_addr;
    uint32_t src_addr;
    uint32_t packet_id;
    uint8_t  flags;         /* hop_limit[2:0] | want_ack[3] | via_mqtt[4] | hop_start[7:5] */
    uint8_t  channel;       /* channel hash (added 2.3) */
    uint8_t  next_hop;      /* next-hop node mod 256, 0 = direct (added 2.5) */
    uint8_t  relay_node;    /* last relaying node mod 256, 0 = none (added 2.5) */
} mesh_header_t;

/* Decoded/encoded mesh packet (application layer) */
typedef struct {
    uint32_t dst_addr;
    uint32_t src_addr;
    uint32_t packet_id;
    uint8_t  hop_limit;   /* current hop count remaining */
    uint8_t  hop_start;   /* hop_limit at origin — hop_start - hop_limit = hops taken */
    bool     want_ack;
    uint8_t  relay_node;  /* 0 = received direct, non-zero = last relay node (low 8 bits) */

    /* AES-decrypted protobuf payload */
    uint8_t  payload[MAX_LORA_PAYLOAD - MESH_HEADER_LEN];
    uint8_t  payload_len;
} mesh_packet_t;

/* Build a NodeInfo TX packet.
 * node_addr: our own 32-bit address
 * mac: 6-byte MAC address
 * out_buf: output LoRa payload (caller supplies buffer ≥ MAX_LORA_PAYLOAD)
 * Returns number of bytes to transmit */
size_t packet_build_nodeinfo(uint32_t node_addr, const uint8_t mac[6],
                             uint8_t *out_buf);

/* Attempt to decode an incoming raw LoRa packet.
 * raw: received bytes, raw_len: number of bytes
 * out: populated on success
 * Returns true if header length is valid (channel validation is implicit via AES+protobuf) */
bool packet_decode(const uint8_t *raw, uint8_t raw_len, mesh_packet_t *out);

/* Global TX packet counter (incremented on each send) */
extern uint32_t g_tx_packet_id;
