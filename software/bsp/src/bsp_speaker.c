#include "bsp_speaker.h"

#include <stdbool.h>
#include <inttypes.h>
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
    BSP_SPEAKER_DEFAULT_VOLUME = 5,
    SPEAKER_SAMPLE_RATE_HZ = 16000,
    SPEAKER_FRAMES_PER_BUFFER = 128,
    SPEAKER_MAX_VOLUME = 50,
    SPEAKER_TEST_TONE_HZ = 1000,
    SPEAKER_TEST_VOLUME = 10,
    SPEAKER_TEST_DURATION_MS = 1000,
    SPEAKER_TEST_AMPLITUDE = 12000,
};

static i2s_chan_handle_t s_tx_channel;
static bool s_speaker_started;
static uint32_t s_sample_rate_hz = SPEAKER_SAMPLE_RATE_HZ;

static uint8_t clamp_volume(uint8_t volume)
{
    if (volume > SPEAKER_MAX_VOLUME) {
        volume = SPEAKER_MAX_VOLUME;
    }
    return volume;
}

static int16_t speaker_scale_sample(int16_t sample, uint8_t volume)
{
    return (int16_t)((int32_t)sample * clamp_volume(volume) / SPEAKER_MAX_VOLUME);
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
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_sample_rate_hz),
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

esp_err_t bsp_speaker_set_sample_rate(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid speaker sample rate");
    ESP_RETURN_ON_ERROR(bsp_speaker_init(), TAG, "Speaker init failed");

    if (s_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }

    const bool was_started = s_speaker_started;
    if (was_started) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_channel), TAG,
                            "Failed to disable I2S before clock change");
        s_speaker_started = false;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    esp_err_t result = i2s_channel_reconfig_std_clock(s_tx_channel, &clk_cfg);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure speaker sample rate to %" PRIu32
                      " Hz: %s",
                 sample_rate_hz, esp_err_to_name(result));
        if (was_started) {
            (void)i2s_channel_enable(s_tx_channel);
            s_speaker_started = true;
        }
        return result;
    }

    s_sample_rate_hz = sample_rate_hz;

    if (was_started) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel), TAG,
                            "Failed to re-enable I2S after clock change");
        s_speaker_started = true;
    }

    ESP_LOGI(TAG, "Speaker sample rate set to %" PRIu32 " Hz", sample_rate_hz);
    return ESP_OK;
}

esp_err_t bsp_speaker_start(void)
{
    ESP_RETURN_ON_ERROR(bsp_speaker_init(), TAG, "Speaker init failed");

    if (!s_speaker_started) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel), TAG,
                            "Failed to enable I2S speaker channel");

        esp_err_t result = gpio_set_level(BSP_AMP_SD_GPIO, 1);
        if (result != ESP_OK) {
            i2s_channel_disable(s_tx_channel);
            ESP_LOGE(TAG, "Failed to enable amplifier: %s",
                     esp_err_to_name(result));
            return result;
        }
        s_speaker_started = true;
    }

    return ESP_OK;
}

esp_err_t bsp_speaker_write(const int16_t *samples, size_t sample_count,
                            uint8_t volume)
{
    ESP_RETURN_ON_FALSE(samples != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Speaker samples are NULL");
    ESP_RETURN_ON_ERROR(bsp_speaker_start(), TAG, "Speaker start failed");

    int16_t scaled_samples[SPEAKER_FRAMES_PER_BUFFER * 2U];
    size_t sample_index = 0;

    while (sample_index < sample_count) {
        const size_t samples_this_buffer =
            (sample_count - sample_index) > (SPEAKER_FRAMES_PER_BUFFER * 2U)
                ? (SPEAKER_FRAMES_PER_BUFFER * 2U)
                : (sample_count - sample_index);

        for (size_t i = 0; i < samples_this_buffer; ++i) {
            scaled_samples[i] = speaker_scale_sample(samples[sample_index + i],
                                                     volume);
        }

        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_channel, scaled_samples,
                              samples_this_buffer * sizeof(scaled_samples[0]),
                              &bytes_written, 1000),
            TAG, "Failed to write speaker samples");
        sample_index += samples_this_buffer;
    }

    return ESP_OK;
}

esp_err_t bsp_speaker_stop(void)
{
    if (!s_speaker_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AMP_SD_GPIO, 0), TAG,
                        "Failed to mute amplifier");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_channel), TAG,
                        "Failed to disable I2S speaker channel");
    s_speaker_started = false;
    return ESP_OK;
}

esp_err_t bsp_speaker_run_self_test(void)
{
    ESP_RETURN_ON_ERROR(bsp_speaker_set_sample_rate(SPEAKER_SAMPLE_RATE_HZ),
                        TAG, "Failed to set speaker self-test sample rate");

    const uint32_t total_frames =
        (SPEAKER_SAMPLE_RATE_HZ * SPEAKER_TEST_DURATION_MS) / 1000U;
    const uint32_t half_period_frames =
        SPEAKER_SAMPLE_RATE_HZ / (SPEAKER_TEST_TONE_HZ * 2U);
    int16_t samples[SPEAKER_FRAMES_PER_BUFFER * 2U];
    uint32_t frames_written = 0;

    ESP_LOGI(TAG, "Speaker self-test tone started: %d Hz, volume=%d/50",
             SPEAKER_TEST_TONE_HZ, SPEAKER_TEST_VOLUME);

    while (frames_written < total_frames) {
        const uint32_t frames_this_buffer =
            (total_frames - frames_written) > SPEAKER_FRAMES_PER_BUFFER
                ? SPEAKER_FRAMES_PER_BUFFER
                : (total_frames - frames_written);

        for (uint32_t i = 0; i < frames_this_buffer; ++i) {
            const uint32_t frame = frames_written + i;
            const int16_t sample =
                ((frame / half_period_frames) & 1U) == 0U
                    ? SPEAKER_TEST_AMPLITUDE
                    : -SPEAKER_TEST_AMPLITUDE;
            samples[i * 2U] = sample;
            samples[i * 2U + 1U] = sample;
        }

        ESP_RETURN_ON_ERROR(bsp_speaker_write(samples, frames_this_buffer * 2U,
                                              SPEAKER_TEST_VOLUME),
                            TAG, "Failed to write speaker self-test tone");
        frames_written += frames_this_buffer;
    }

    ESP_RETURN_ON_ERROR(bsp_speaker_stop(), TAG,
                        "Failed to stop speaker self-test tone");
    ESP_LOGI(TAG, "Speaker self-test tone finished");
    return ESP_OK;
}
