#pragma once
#include <stdint.h>

/* Derives the RF centre frequency for a Meshtastic channel using the same
 * algorithm as the official firmware (DJB2 hash → channel slot → freq). */
uint32_t mesh_channel_freq_hz(const char *channel_name, float bw_khz,
                               uint32_t freq_start_hz, uint32_t freq_end_hz);
