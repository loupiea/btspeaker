#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount the TF/SD card in SPI mode at /sdcard. */
esp_err_t bsp_sd_mount(void);

/** Print card identity, capacity and bus information. */
void bsp_sd_print_info(void);

/** List entries in the root directory. */
esp_err_t bsp_sd_list_root(void);

/** Write and read back /sdcard/btspeaker_test.txt. */
esp_err_t bsp_sd_run_file_test(void);

/** Read /sdcard/MUSIC.MP3 and report header bytes plus read speed. */
esp_err_t bsp_sd_run_mp3_read_test(void);

/** Manually send SPI CMD0 to check whether the TF/SD card responds in SPI mode. */
esp_err_t bsp_sd_run_cmd0_probe(void);

/** Unmount the card and release the SPI bus. */
void bsp_sd_unmount(void);

#ifdef __cplusplus
}
#endif
