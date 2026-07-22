#include "bsp_speaker.h"

#include <stdint.h>
#include <stddef.h>

#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_speaker";

enum {
    BSP_SPEAKER_DEFAULT_VOLUME = 1,
    SPEAKER_SAMPLE_RATE_HZ = 16000,
    SPEAKER_TEST_TONE_HZ = 1000,
    SPEAKER_TEST_DURATION_MS = 1000,
    SPEAKER_FRAMES_PER_BUFFER = 128,
    SPEAKER_MAX_VOLUME = 10,
};

static i2s_chan_handle_t s_tx_channel;

static int16_t speaker_sample_for_volume(uint8_t volume, uint32_t frame_index)
{
    if (volume > SPEAKER_MAX_VOLUME) {
        volume = SPEAKER_MAX_VOLUME;
    }

    const uint32_t frames_per_half_cycle =
        SPEAKER_SAMPLE_RATE_HZ / (SPEAKER_TEST_TONE_HZ * 2U);
    const int16_t amplitude = (int16_t)(volume * 2000);

    return ((frame_index / frames_per_half_cycle) % 2U) == 0U
               ? amplitude
               : (int16_t)-amplitude;
}

esp_err_t bsp_speaker_init(void)
{
    if (s_tx_channel != NULL) {
        return ESP_OK;
    }

    const gpio_config_t amp_sd_config = {
        .pin_bit_mask = 1ULL << BSP_AMP_SD_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&amp_sd_config), TAG,
                        "Failed to configure amplifier enable");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AMP_SD_GPIO, 0), TAG,
                        "Failed to mute amplifier");

    const i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_channel, NULL), TAG,
                        "Failed to allocate I2S0 TX channel");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPEAKER_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BSP_AMP_BCLK_GPIO,
            .ws = BSP_AMP_LRCLK_GPIO,
            .dout = BSP_AMP_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t result = i2s_channel_init_std_mode(s_tx_channel, &std_cfg);
    if (result != ESP_OK) {
        i2s_del_channel(s_tx_channel);
        s_tx_channel = NULL;
        ESP_LOGE(TAG, "Failed to initialize I2S speaker output: %s",
                 esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "Speaker I2S initialized: BCLK=%d, LRCLK=%d, DOUT=%d, SD=%d",
             BSP_AMP_BCLK_GPIO, BSP_AMP_LRCLK_GPIO,
             BSP_AMP_DOUT_GPIO, BSP_AMP_SD_GPIO);
    return ESP_OK;
}

esp_err_t bsp_speaker_run_self_test(void)
{
    ESP_RETURN_ON_ERROR(bsp_speaker_init(), TAG, "Speaker init failed");

    int16_t samples[SPEAKER_FRAMES_PER_BUFFER * 2U];
    const uint32_t total_frames =
        SPEAKER_SAMPLE_RATE_HZ * SPEAKER_TEST_DURATION_MS / 1000U;
    uint32_t frame_index = 0;

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel), TAG,
                        "Failed to enable I2S speaker channel");

    esp_err_t result = gpio_set_level(BSP_AMP_SD_GPIO, 1);
    if (result != ESP_OK) {
        i2s_channel_disable(s_tx_channel);
        ESP_LOGE(TAG, "Failed to enable amplifier: %s", esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "Speaker self-test tone started: %d Hz, volume=%d/10",
             SPEAKER_TEST_TONE_HZ, BSP_SPEAKER_DEFAULT_VOLUME);

    while (frame_index < total_frames) {
        const uint32_t frames_this_buffer =
            (total_frames - frame_index) > SPEAKER_FRAMES_PER_BUFFER
                ? SPEAKER_FRAMES_PER_BUFFER
                : (total_frames - frame_index);

        for (uint32_t i = 0; i < frames_this_buffer; ++i) {
            const int16_t sample =
                speaker_sample_for_volume(BSP_SPEAKER_DEFAULT_VOLUME,
                                          frame_index + i);
            samples[i * 2U] = sample;
            samples[i * 2U + 1U] = sample;
        }

        size_t bytes_written = 0;
        result =
            i2s_channel_write(s_tx_channel, samples,
                              frames_this_buffer * 2U * sizeof(samples[0]),
                              &bytes_written, 1000);
        if (result != ESP_OK) {
            gpio_set_level(BSP_AMP_SD_GPIO, 0);
            i2s_channel_disable(s_tx_channel);
            ESP_LOGE(TAG, "Failed to write speaker samples: %s",
                     esp_err_to_name(result));
            return result;
        }

        frame_index += frames_this_buffer;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AMP_SD_GPIO, 0), TAG,
                        "Failed to mute amplifier after self-test");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_channel), TAG,
                        "Failed to disable I2S speaker channel");

    ESP_LOGI(TAG, "Speaker self-test tone finished");
    return ESP_OK;
}
