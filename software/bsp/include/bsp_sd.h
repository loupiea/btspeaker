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

/** Unmount the card and release the SPI bus. */
void bsp_sd_unmount(void);

#ifdef __cplusplus
}
#endif
