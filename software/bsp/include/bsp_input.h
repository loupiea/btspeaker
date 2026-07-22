#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool play_pressed;
    bool source_pressed;
    int16_t encoder_delta;
} bsp_input_event_t;

/** Configure the two buttons and rotary encoder GPIO inputs. */
esp_err_t bsp_input_init(void);

/**
 * Poll and decode input events.
 *
 * Call every 10 ms. Button events are generated on the debounced press edge.
 * Encoder edges are captured by GPIO interrupts, so encoder_delta may contain
 * more than one step if the encoder turns quickly between two polls.
 */
void bsp_input_poll(bsp_input_event_t *event);

#ifdef __cplusplus
}
#endif
