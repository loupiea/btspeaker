#include "bsp_aux.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "bsp_pins.h"
#include "bsp_speaker.h"
#include "bsp_volume.h"
#include "driver/i2c.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_aux";

enum {
    AUX_I2C_PORT = I2C_NUM_0,
    ES8388_I2C_ADDRESS = 0x10,
    ES8388_ALT_I2C_ADDRESS = 0x11,
    AUX_I2S_PORT = I2S_NUM_1,
    AUX_SAMPLE_RATE_HZ = 16000,
    AUX_FRAMES_PER_BUFFER = 128,
    AUX_SILENCE_THRESHOLD = 32,
    AUX_PLAYER_STACK_SIZE = 4096,
    AUX_PLAYER_TASK_PRIORITY = 2,
    AUX_PLAYER_TASK_CORE = 1,
    AUX_STOP_WAIT_MS = 2000,
};

static i2s_chan_handle_t s_rx_channel;
static TaskHandle_t s_aux_task;
static volatile bool s_stop_requested;

static int16_t aux_abs_sample(int16_t sample)
{
    return sample == INT16_MIN ? INT16_MAX : (int16_t)(sample < 0 ? -sample : sample);
}

static int16_t aux_sample_peak(const int16_t *samples, size_t sample_count)
{
    int16_t peak = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        const int16_t value = aux_abs_sample(samples[i]);
        if (value > peak) {
            peak = value;
        }
    }
    return peak;
}

static esp_err_t es8388_write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    esp_err_t result =
        i2c_master_write_to_device(AUX_I2C_PORT, ES8388_I2C_ADDRESS,
                                   data, sizeof(data), pdMS_TO_TICKS(100));
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "ES8388 register write failed: address=0x%02X, reg=0x%02X, value=0x%02X, error=%s",
                 ES8388_I2C_ADDRESS, reg, value, esp_err_to_name(result));
    }
    return result;
}

static esp_err_t es8388_probe_address(uint8_t address)
{
    i2c_cmd_handle_t command = i2c_cmd_link_create();
    ESP_RETURN_ON_FALSE(command != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to allocate I2C probe command");

    esp_err_t result = i2c_master_start(command);
    if (result == ESP_OK) {
        result = i2c_master_write_byte(command, (uint8_t)((address << 1U) | I2C_MASTER_WRITE),
                                       true);
    }
    if (result == ESP_OK) {
        result = i2c_master_stop(command);
    }
    if (result == ESP_OK) {
        result = i2c_master_cmd_begin(AUX_I2C_PORT, command, pdMS_TO_TICKS(100));
    }
    i2c_cmd_link_delete(command);

    ESP_LOGI(TAG, "ES8388 probe: address=0x%02X, result=%s",
             address, esp_err_to_name(result));
    return result;
}

static esp_err_t es8388_init_adc(void)
{
    const esp_err_t primary_probe = es8388_probe_address(ES8388_I2C_ADDRESS);
    const esp_err_t alt_probe = es8388_probe_address(ES8388_ALT_I2C_ADDRESS);
    if (primary_probe != ESP_OK) {
        ESP_LOGE(TAG,
                 "ES8388 did not ACK expected address 0x%02X; alternate 0x%02X probe=%s",
                 ES8388_I2C_ADDRESS, ES8388_ALT_I2C_ADDRESS,
                 esp_err_to_name(alt_probe));
        return primary_probe;
    }

    static const uint8_t init[][2] = {
        {0x00, 0x80}, /* reset */
        {0x00, 0x06}, /* normal control */
        {0x01, 0x50}, /* enable internal bias/reference */
        {0x02, 0x00}, /* power up main blocks */
        {0x03, 0x00}, /* power up ADC and line input */
        {0x08, 0x00}, /* I2S slave mode */
        {0x09, 0x00}, /* ADC PGA gain 0 dB */
        {0x0A, 0x00}, /* select LIN1/RIN1 */
        {0x0B, 0x00}, /* I2S format */
        {0x0C, 0x0C}, /* 16-bit sample width */
        {0x0D, 0x02}, /* MCLK/LRCLK ratio 256 */
        {0x10, 0x00}, /* ADC digital volume left */
        {0x11, 0x00}, /* ADC digital volume right */
    };

    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        ESP_RETURN_ON_ERROR(es8388_write_reg(init[i][0], init[i][1]), TAG,
                            "Failed to write ES8388 register 0x%02X",
                            init[i][0]);
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    ESP_LOGI(TAG, "ES8388 ADC initialized: address=0x%02X", ES8388_I2C_ADDRESS);
    return ESP_OK;
}

static esp_err_t aux_i2s_init(void)
{
    if (s_rx_channel != NULL) {
        return ESP_OK;
    }

    const i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(AUX_I2S_PORT, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_channel), TAG,
                        "Failed to allocate I2S1 RX channel");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUX_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_AUX_MCLK_GPIO,
            .bclk = BSP_AUX_BCLK_GPIO,
            .ws = BSP_AUX_LRCLK_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = BSP_AUX_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t result = i2s_channel_init_std_mode(s_rx_channel, &std_cfg);
    if (result != ESP_OK) {
        i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        ESP_LOGE(TAG, "Failed to initialize AUX I2S input: %s",
                 esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "AUX I2S initialized: MCLK=%d, BCLK=%d, LRCLK=%d, DIN=%d",
             BSP_AUX_MCLK_GPIO, BSP_AUX_BCLK_GPIO,
             BSP_AUX_LRCLK_GPIO, BSP_AUX_DIN_GPIO);
    return ESP_OK;
}

esp_err_t bsp_aux_init(void)
{
    ESP_RETURN_ON_ERROR(es8388_init_adc(), TAG, "ES8388 init failed");
    return aux_i2s_init();
}

static esp_err_t run_aux_playback(void)
{
    ESP_RETURN_ON_ERROR(bsp_aux_init(), TAG, "AUX init failed");
    ESP_RETURN_ON_ERROR(bsp_speaker_set_sample_rate(AUX_SAMPLE_RATE_HZ), TAG,
                        "Failed to set AUX playback sample rate");
    ESP_RETURN_ON_ERROR(bsp_speaker_start(), TAG, "Speaker start failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_channel), TAG,
                        "Failed to enable AUX I2S input");

    int16_t samples[AUX_FRAMES_PER_BUFFER * 2U];
    uint32_t blocks_read = 0;
    uint32_t silent_blocks = 0;
    int16_t max_peak = 0;

    ESP_LOGI(TAG, "AUX playback started");

    while (!s_stop_requested) {
        size_t bytes_read = 0;
        esp_err_t result =
            i2s_channel_read(s_rx_channel, samples, sizeof(samples),
                             &bytes_read, 1000);
        if (result != ESP_OK) {
            i2s_channel_disable(s_rx_channel);
            bsp_speaker_stop();
            ESP_LOGE(TAG, "Failed to read AUX samples: %s",
                     esp_err_to_name(result));
            return result;
        }

        if (bytes_read > 0) {
            const size_t samples_read = bytes_read / sizeof(samples[0]);
            const int16_t peak = aux_sample_peak(samples, samples_read);

            ++blocks_read;
            if (peak > max_peak) {
                max_peak = peak;
            }
            if (peak < AUX_SILENCE_THRESHOLD) {
                ++silent_blocks;
            }

            result = bsp_speaker_write(samples, samples_read,
                                       bsp_volume_get());
            if (result != ESP_OK) {
                i2s_channel_disable(s_rx_channel);
                bsp_speaker_stop();
                ESP_LOGE(TAG, "Failed to write AUX samples to speaker: %s",
                         esp_err_to_name(result));
                return result;
            }
        }
    }

    ESP_LOGI(TAG, "AUX samples: blocks=%" PRIu32 ", silent_blocks=%" PRIu32
             ", max_peak=%d",
             blocks_read, silent_blocks, max_peak);

    esp_err_t result = i2s_channel_disable(s_rx_channel);
    const esp_err_t speaker_result = bsp_speaker_stop();
    ESP_RETURN_ON_ERROR(result, TAG, "Failed to disable AUX I2S input");
    ESP_RETURN_ON_ERROR(speaker_result, TAG, "Speaker stop failed");

    ESP_LOGI(TAG, "AUX playback stopped");
    return ESP_OK;
}

static void aux_player_task(void *argument)
{
    (void)argument;
    const esp_err_t result = run_aux_playback();
    if (result != ESP_OK && !s_stop_requested) {
        ESP_LOGE(TAG, "AUX playback failed: %s", esp_err_to_name(result));
    }
    s_aux_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bsp_aux_start(void)
{
    if (s_aux_task != NULL) {
        return ESP_OK;
    }

    s_stop_requested = false;
    const BaseType_t created =
        xTaskCreatePinnedToCore(aux_player_task, "aux_player",
                                AUX_PLAYER_STACK_SIZE, NULL,
                                AUX_PLAYER_TASK_PRIORITY, &s_aux_task,
                                AUX_PLAYER_TASK_CORE);
    if (created != pdPASS) {
        s_aux_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void bsp_aux_stop(void)
{
    s_stop_requested = true;
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(AUX_STOP_WAIT_MS);
    while (s_aux_task != NULL &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
