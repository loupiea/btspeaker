#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start looping /sdcard/MUSIC.WAV from the TF card. */
esp_err_t bsp_tf_player_start(void);

/** Stop TF playback, close the file and unmount the card. */
void bsp_tf_player_stop(void);

#ifdef __cplusplus
}
#endif
