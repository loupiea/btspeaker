#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize SPI3 and place both shared-bus devices in the deselected state. */
esp_err_t bsp_ch376_init(void);

/** Run CHECK_EXIST. Returns ESP_OK only when response equals the bitwise inverse. */
esp_err_t bsp_ch376_check_exist(uint8_t challenge, uint8_t *response);

/** Read the raw CH376S chip and firmware version byte. */
esp_err_t bsp_ch376_get_version(uint8_t *version);

/** Release the CH376S SPI device and SPI3 bus. */
void bsp_ch376_deinit(void);

#ifdef __cplusplus
}
#endif
