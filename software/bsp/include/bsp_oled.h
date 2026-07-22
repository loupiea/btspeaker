#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the board OLED over the shared I2C bus. */
esp_err_t bsp_oled_init(void);

/** Show a simple full-screen pattern so the panel can be visually checked. */
esp_err_t bsp_oled_run_self_test(void);

#ifdef __cplusplus
}
#endif
