#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MESH_HEADER_LEN     13
#define MAX_LORA_PAYLOAD    255

/* Raw OTA packet header */
typedef struct __attribute__((packed)) {
    uint32_t dst_addr;
    uint32_t src_addr;
    uint32_t packet_id;
    uint8_t  flags;         /* hop_limit[2:0] | want_ack[3] | channel_hash_nibble[7:4] */
} mesh_header_t;

/* Decoded/encoded mesh packet (application layer) */
typedef struct {
    uint32_t dst_addr;
    uint32_t src_addr;
    uint32_t packet_id;
    uint8_t  hop_limit;
    bool     want_ack;

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
 * Returns true if header is valid and channel hash matches */
bool packet_decode(const uint8_t *raw, uint8_t raw_len, mesh_packet_t *out);

/* Global TX packet counter (incremented on each send) */
extern uint32_t g_tx_packet_id;
