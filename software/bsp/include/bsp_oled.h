#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the board OLED over the shared I2C bus. */
esp_err_t bsp_oled_init(void);

/** Clear all OLED pixels. */
esp_err_t bsp_oled_clear(void);

/** Show two ASCII text lines on the OLED. */
esp_err_t bsp_oled_show_lines(const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
