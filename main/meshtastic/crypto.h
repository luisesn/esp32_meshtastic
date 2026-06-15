#pragma once
#include <stdint.h>
#include <stddef.h>

void    crypto_init(void);
uint8_t crypto_get_channel_hash(void);

/* Encrypt or decrypt len bytes of data in-place using AES-128-CTR.
 * packet_id and sender_addr are used to construct the CTR nonce. */
void crypto_ctr(uint8_t *data, size_t len,
                uint32_t packet_id, uint32_t sender_addr);
