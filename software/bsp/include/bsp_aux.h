#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize ES8388 AUX capture over I2C and I2S1. */
esp_err_t bsp_aux_init(void);

/** Start continuous AUX input playback through the speaker path. */
esp_err_t bsp_aux_start(void);

/** Stop AUX input playback and release the speaker output. */
void bsp_aux_stop(void);

#ifdef __cplusplus
}
#endif
