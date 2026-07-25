#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BSP_VOLUME_DEFAULT = 5,
    BSP_VOLUME_MAX = 50,
};

/** Return the current shared speaker volume, in the range 0..50. */
uint8_t bsp_volume_get(void);

/** Set the shared speaker volume, clamped to 0..50. */
void bsp_volume_set(uint8_t volume);

/** Adjust the shared speaker volume by a signed encoder delta. */
void bsp_volume_adjust(int16_t delta);

#ifdef __cplusplus
}
#endif
