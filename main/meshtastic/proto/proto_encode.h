#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "portnums.h"

/* Minimal Meshtastic protobuf encode/decode — wire-format compatible with
 * the nanopb-generated files that official firmware uses.
 * Only the fields required for NodeInfo TX and basic RX decode are implemented. */

/* Encoded User message (for NodeInfo payload) */
typedef struct {
    char    id[16];          /* "!<hex addr>" */
    char    long_name[40];
    char    short_name[5];
    uint8_t macaddr[6];
    uint32_t hw_model;
    bool    is_licensed;
} mesh_user_t;

/* Encoded Data message (MeshPacket.decoded / inner payload) */
typedef struct {
    meshtastic_PortNum portnum;
    uint8_t  payload[240];
    uint16_t payload_len;
    bool     want_response;
} mesh_data_t;

/* Encode a User message into buf, return number of bytes written (0 on error) */
size_t proto_encode_user(const mesh_user_t *user, uint8_t *buf, size_t buf_size);

/* Encode a Data message into buf, return number of bytes written (0 on error) */
size_t proto_encode_data(const mesh_data_t *data, uint8_t *buf, size_t buf_size);

/* Decode a Data message from buf, return true on success */
bool proto_decode_data(const uint8_t *buf, size_t len, mesh_data_t *out);

/* Decode a User message from buf, return true on success */
bool proto_decode_user(const uint8_t *buf, size_t len, mesh_user_t *out);
