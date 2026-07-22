#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the I2S speaker output path. */
esp_err_t bsp_speaker_init(void);

/** Play a short tone at the default test volume. */
esp_err_t bsp_speaker_run_self_test(void);

#ifdef __cplusplus
}
#endif
