#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_MODE_BLUETOOTH = 0,
    BSP_MODE_USB,
    BSP_MODE_TF,
    BSP_MODE_AUX,
} bsp_mode_t;

/** Return the next source mode in the user-facing cycle. */
bsp_mode_t bsp_mode_next(bsp_mode_t mode);

/** Return the display name for a source mode. */
const char *bsp_mode_name(bsp_mode_t mode);

#ifdef __cplusplus
}
#endif
