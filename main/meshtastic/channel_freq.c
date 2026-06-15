#include "channel_freq.h"

uint32_t mesh_channel_freq_hz(const char *channel_name, float bw_khz,
                               uint32_t freq_start_hz, uint32_t freq_end_hz)
{
    uint32_t h = 5381;
    for (const char *p = channel_name; *p; p++)
        h = ((h << 5) + h) + (uint8_t)*p;

    uint32_t bw_hz  = (uint32_t)(bw_khz * 1000.0f);
    uint32_t num_ch = (freq_end_hz - freq_start_hz) / bw_hz;
    uint32_t ch_num = h % num_ch;
    return freq_start_hz + bw_hz / 2 + ch_num * bw_hz;
}
