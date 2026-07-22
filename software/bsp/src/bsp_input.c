#include "bsp_input.h"

#include <stddef.h>

#include "bsp_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define BUTTON_DEBOUNCE_SAMPLES 3U
#define ENCODER_TRANSITIONS_PER_STEP 4

typedef struct {
    bool last_raw_pressed;
    bool stable_pressed;
    uint8_t stable_samples;
} button_state_t;

static const char *TAG = "bsp_input";
static button_state_t s_play_button;
static button_state_t s_source_button;
static uint8_t s_encoder_previous;
static int8_t s_encoder_accumulator;
static volatile int16_t s_encoder_pending_steps;
static portMUX_TYPE s_encoder_lock = portMUX_INITIALIZER_UNLOCKED;

static const int8_t s_encoder_transition_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static bool button_is_pressed(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
}

static bool button_pressed_event(gpio_num_t gpio, button_state_t *state)
{
    const bool raw_pressed = button_is_pressed(gpio);

    if (raw_pressed != state->last_raw_pressed) {
        state->last_raw_pressed = raw_pressed;
        state->stable_samples = 0;
        return false;
    }

    if (state->stable_samples < BUTTON_DEBOUNCE_SAMPLES) {
        state->stable_samples++;
    }

    if ((state->stable_samples == BUTTON_DEBOUNCE_SAMPLES) &&
        (raw_pressed != state->stable_pressed)) {
        state->stable_pressed = raw_pressed;
        return raw_pressed;
    }

    return false;
}

static uint8_t encoder_read_state(void)
{
    const uint8_t a = (uint8_t)gpio_get_level(BSP_ENCODER_A_GPIO);
    const uint8_t b = (uint8_t)gpio_get_level(BSP_ENCODER_B_GPIO);
    return (uint8_t)((a << 1U) | b);
}

static void encoder_edge_isr(void *argument)
{
    (void)argument;

    const uint8_t encoder_current = encoder_read_state();

    portENTER_CRITICAL_ISR(&s_encoder_lock);
    const uint8_t transition = (uint8_t)((s_encoder_previous << 2U) | encoder_current);
    s_encoder_previous = encoder_current;
    s_encoder_accumulator += s_encoder_transition_table[transition];

    if (s_encoder_accumulator >= ENCODER_TRANSITIONS_PER_STEP) {
        s_encoder_pending_steps++;
        s_encoder_accumulator -= ENCODER_TRANSITIONS_PER_STEP;
    } else if (s_encoder_accumulator <= -ENCODER_TRANSITIONS_PER_STEP) {
        s_encoder_pending_steps--;
        s_encoder_accumulator += ENCODER_TRANSITIONS_PER_STEP;
    }
    portEXIT_CRITICAL_ISR(&s_encoder_lock);
}

esp_err_t bsp_input_init(void)
{
    const gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BSP_BUTTON_PLAY_GPIO) |
                        (1ULL << BSP_BUTTON_SOURCE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t encoder_config = {
        .pin_bit_mask = (1ULL << BSP_ENCODER_A_GPIO) |
                        (1ULL << BSP_ENCODER_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t error = gpio_config(&button_config);
    if (error != ESP_OK) {
        return error;
    }

    error = gpio_config(&encoder_config);
    if (error != ESP_OK) {
        return error;
    }

    s_play_button.last_raw_pressed = button_is_pressed(BSP_BUTTON_PLAY_GPIO);
    s_play_button.stable_pressed = s_play_button.last_raw_pressed;
    s_play_button.stable_samples = 0;

    s_source_button.last_raw_pressed = button_is_pressed(BSP_BUTTON_SOURCE_GPIO);
    s_source_button.stable_pressed = s_source_button.last_raw_pressed;
    s_source_button.stable_samples = 0;

    s_encoder_previous = encoder_read_state();
    s_encoder_accumulator = 0;
    s_encoder_pending_steps = 0;

    error = gpio_install_isr_service(0);
    if ((error != ESP_OK) && (error != ESP_ERR_INVALID_STATE)) {
        return error;
    }

    error = gpio_isr_handler_add(BSP_ENCODER_A_GPIO, encoder_edge_isr, NULL);
    if (error != ESP_OK) {
        return error;
    }

    error = gpio_isr_handler_add(BSP_ENCODER_B_GPIO, encoder_edge_isr, NULL);
    if (error != ESP_OK) {
        gpio_isr_handler_remove(BSP_ENCODER_A_GPIO);
        return error;
    }

    ESP_LOGI(TAG, "Inputs initialized: PLAY=GPIO%d, SOURCE=GPIO%d, ENC_A=GPIO%d, ENC_B=GPIO%d (interrupt mode)",
             BSP_BUTTON_PLAY_GPIO,
             BSP_BUTTON_SOURCE_GPIO,
             BSP_ENCODER_A_GPIO,
             BSP_ENCODER_B_GPIO);
    ESP_LOGI(TAG, "Encoder initial levels: A=%u, B=%u",
             (unsigned int)((s_encoder_previous >> 1U) & 1U),
             (unsigned int)(s_encoder_previous & 1U));
    return ESP_OK;
}

void bsp_input_poll(bsp_input_event_t *event)
{
    if (event == NULL) {
        return;
    }

    event->play_pressed = button_pressed_event(BSP_BUTTON_PLAY_GPIO, &s_play_button);
    event->source_pressed = button_pressed_event(BSP_BUTTON_SOURCE_GPIO, &s_source_button);

    portENTER_CRITICAL(&s_encoder_lock);
    event->encoder_delta = s_encoder_pending_steps;
    s_encoder_pending_steps = 0;
    portEXIT_CRITICAL(&s_encoder_lock);
}
