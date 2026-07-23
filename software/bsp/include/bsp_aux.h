#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize ES8388 AUX capture over I2C and I2S1. */
esp_err_t bsp_aux_init(void);

/** Play AUX input through the speaker path for a short hardware test. */
esp_err_t bsp_aux_run_self_test(void);

#ifdef __cplusplus
}
#endif
