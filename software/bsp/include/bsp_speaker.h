#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the I2S speaker output path. */
esp_err_t bsp_speaker_init(void);

/** Enable the speaker stream output path. */
esp_err_t bsp_speaker_start(void);

/** Write interleaved stereo PCM samples to the speaker path. */
esp_err_t bsp_speaker_write(const int16_t *samples, size_t sample_count,
                            uint8_t volume);

/** Reconfigure the speaker stream sample rate. */
esp_err_t bsp_speaker_set_sample_rate(uint32_t sample_rate_hz);

/** Stop the speaker stream output path and mute the amplifiers. */
esp_err_t bsp_speaker_stop(void);

/** Play a short boot-time tone to verify the amplifier and I2S path. */
esp_err_t bsp_speaker_run_self_test(void);

#ifdef __cplusplus
}
#endif
