#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start playing /MUSIC.WAV from the USB disk through CH376S. */
esp_err_t bsp_usb_player_start(void);

/** Request the USB playback task to stop. */
void bsp_usb_player_stop(void);

#ifdef __cplusplus
}
#endif
