#include "bsp_volume.h"

static uint8_t s_volume = BSP_VOLUME_DEFAULT;

uint8_t bsp_volume_get(void)
{
    return s_volume;
}

void bsp_volume_set(uint8_t volume)
{
    if (volume > BSP_VOLUME_MAX) {
        volume = BSP_VOLUME_MAX;
    }
    s_volume = volume;
}

void bsp_volume_adjust(int16_t delta)
{
    int16_t next_volume = (int16_t)s_volume + delta;
    if (next_volume < 0) {
        next_volume = 0;
    } else if (next_volume > BSP_VOLUME_MAX) {
        next_volume = BSP_VOLUME_MAX;
    }
    s_volume = (uint8_t)next_volume;
}
