#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start Bluetooth Classic A2DP sink playback to the speaker output. */
esp_err_t bsp_bluetooth_a2dp_sink_start(void);

#ifdef __cplusplus
}
#endif
