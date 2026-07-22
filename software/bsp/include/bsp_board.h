#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize board-level hardware to a safe default state. */
void bsp_board_init(void);

/** Print MCU and flash information through the ESP-IDF log output. */
void bsp_board_print_info(void);

#ifdef __cplusplus
}
#endif
