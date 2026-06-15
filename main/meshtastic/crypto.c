#include <string.h>
#include "aes/esp_aes.h"
#include "esp_log.h"
#include "crypto.h"
#include "../config.h"

static const char *TAG = "crypto";

/* Default PSK table from meshtastic/firmware src/mesh/Channels.h — do not modify */
static const uint8_t MESHTASTIC_DEFAULT_PSK[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

static uint8_t       g_aes_key[16];
static uint8_t       g_channel_hash;
static esp_aes_context g_aes_ctx;

/* ── PSK expansion — from meshtastic/firmware Channels.cpp ─────────────── */

static void crypto_expand_psk(uint8_t psk_index, uint8_t key_out[16]) {
    if (psk_index == 0) {
        memset(key_out, 0, 16);
        return;
    }
    memcpy(key_out, MESHTASTIC_DEFAULT_PSK, 16);
    key_out[15] += (psk_index - 1);
}

/* ── Channel hash ────────────────────────────────────────────────────────── */

static uint8_t crypto_channel_hash(const char *channel_name, const uint8_t key[16]) {
    uint8_t h = 0;
    for (size_t i = 0; channel_name[i] != '\0'; i++)
        h ^= (uint8_t)channel_name[i];
    for (size_t i = 0; i < 16; i++)
        h ^= key[i];
    return h;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void crypto_init(void) {
    crypto_expand_psk(MESH_PSK_INDEX, g_aes_key);
    g_channel_hash = crypto_channel_hash(MESH_CHANNEL_NAME, g_aes_key);
    esp_aes_init(&g_aes_ctx);

    ESP_LOGI(TAG, "AES key: %02x%02x%02x%02x...%02x (PSK index %d)",
             g_aes_key[0], g_aes_key[1], g_aes_key[2], g_aes_key[3],
             g_aes_key[15], MESH_PSK_INDEX);
    ESP_LOGI(TAG, "Channel hash: 0x%02X (channel=\"%s\")",
             g_channel_hash, MESH_CHANNEL_NAME);
}

uint8_t crypto_get_channel_hash(void) {
    return g_channel_hash;
}

void crypto_ctr(uint8_t *data, size_t len,
                uint32_t packet_id, uint32_t sender_addr) {
    /* Meshtastic CTR nonce layout (16 bytes):
     *   bytes 0–3  : packet_id   (little-endian)
     *   bytes 4–7  : sender_addr (little-endian)
     *   bytes 8–15 : 0x00 padding */
    uint8_t nonce[16] = {0};
    nonce[0] = (uint8_t)(packet_id);
    nonce[1] = (uint8_t)(packet_id >> 8);
    nonce[2] = (uint8_t)(packet_id >> 16);
    nonce[3] = (uint8_t)(packet_id >> 24);
    nonce[4] = (uint8_t)(sender_addr);
    nonce[5] = (uint8_t)(sender_addr >> 8);
    nonce[6] = (uint8_t)(sender_addr >> 16);
    nonce[7] = (uint8_t)(sender_addr >> 24);

    /* ESP hardware AES-CTR — same key setup for both encrypt and decrypt */
    esp_aes_setkey(&g_aes_ctx, g_aes_key, 128);

    size_t  nc_off      = 0;
    uint8_t stream_block[16] = {0};
    esp_aes_crypt_ctr(&g_aes_ctx, len, &nc_off, nonce, stream_block, data, data);
}
