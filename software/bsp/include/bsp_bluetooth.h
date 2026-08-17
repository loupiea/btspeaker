#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start Bluetooth Classic A2DP sink playback to the speaker output. */
esp_err_t bsp_bluetooth_a2dp_sink_start(void);

/** Stop routing Bluetooth PCM to the speaker while keeping BT initialized. */
void bsp_bluetooth_a2dp_sink_stop(void);

#ifdef __cplusplus
}
#endif
