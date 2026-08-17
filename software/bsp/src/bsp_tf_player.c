#include "bsp_tf_player.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_sd.h"
#include "bsp_speaker.h"
#include "bsp_volume.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_tf_player";
static const char TF_MUSIC_FILE[] = "/sdcard/MUSIC.WAV";

enum {
    TF_PLAYER_STACK_SIZE = 6144,
    TF_PLAYER_TASK_PRIORITY = 2,
    TF_PLAYER_TASK_CORE = 1,
    TF_INPUT_SAMPLES = 256,
    TF_STOP_WAIT_MS = 2000,
};

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t file_size;
    char wave[4];
} wav_riff_header_t;

typedef struct __attribute__((packed)) {
    char id[4];
    uint32_t size;
} wav_chunk_header_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_format_t;

typedef struct {
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t data_size;
    long data_offset;
} wav_info_t;

static TaskHandle_t s_tf_player_task;
static volatile bool s_stop_requested;
static int16_t s_input_samples[TF_INPUT_SAMPLES];
static int16_t s_stereo_samples[TF_INPUT_SAMPLES * 2U];

static esp_err_t read_exact(FILE *file, void *buffer, size_t size)
{
    return fread(buffer, 1, size, file) == size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t skip_chunk(FILE *file, uint32_t size)
{
    const long aligned_size = (long)(size + (size & 1U));
    return fseek(file, aligned_size, SEEK_CUR) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t parse_wav_header(FILE *file, wav_info_t *info)
{
    ESP_RETURN_ON_FALSE(file != NULL && info != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "Invalid WAV parser arguments");

    wav_riff_header_t header;
    ESP_RETURN_ON_ERROR(read_exact(file, &header, sizeof(header)), TAG,
                        "Failed to read WAV RIFF header");
    ESP_RETURN_ON_FALSE(memcmp(header.riff, "RIFF", 4) == 0 &&
                            memcmp(header.wave, "WAVE", 4) == 0,
                        ESP_ERR_INVALID_RESPONSE, TAG,
                        "MUSIC.WAV is not a RIFF/WAVE file");

    bool format_found = false;
    bool data_found = false;
    wav_format_t format = {0};

    while (!data_found) {
        wav_chunk_header_t chunk;
        ESP_RETURN_ON_ERROR(read_exact(file, &chunk, sizeof(chunk)), TAG,
                            "Failed to read WAV chunk header");

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            ESP_RETURN_ON_FALSE(chunk.size >= sizeof(format),
                                ESP_ERR_INVALID_SIZE, TAG,
                                "WAV fmt chunk is too small");
            ESP_RETURN_ON_ERROR(read_exact(file, &format, sizeof(format)), TAG,
                                "Failed to read WAV format");
            if (chunk.size > sizeof(format)) {
                ESP_RETURN_ON_ERROR(
                    skip_chunk(file, chunk.size - sizeof(format)), TAG,
                    "Failed to skip extended WAV format");
            }
            format_found = true;
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            ESP_RETURN_ON_FALSE(format_found, ESP_ERR_INVALID_STATE, TAG,
                                "WAV data chunk appeared before fmt chunk");
            info->data_size = chunk.size;
            info->data_offset = ftell(file);
            data_found = true;
        } else {
            ESP_RETURN_ON_ERROR(skip_chunk(file, chunk.size), TAG,
                                "Failed to skip WAV chunk");
        }
    }

    ESP_RETURN_ON_FALSE(format.audio_format == 1, ESP_ERR_NOT_SUPPORTED, TAG,
                        "Only PCM WAV is supported");
    ESP_RETURN_ON_FALSE(format.bits_per_sample == 16,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "Only 16-bit PCM WAV is supported");
    ESP_RETURN_ON_FALSE(format.channels == 1 || format.channels == 2,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "Only mono or stereo WAV is supported");
    ESP_RETURN_ON_FALSE(format.sample_rate >= 8000 &&
                            format.sample_rate <= 48000,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "WAV sample rate must be 8-48 kHz");

    info->channels = format.channels;
    info->sample_rate = format.sample_rate;
    ESP_LOGI(TAG,
             "TF WAV ready: rate=%lu Hz, channels=%u, bits=%u, data=%lu bytes",
             (unsigned long)format.sample_rate, (unsigned)format.channels,
             (unsigned)format.bits_per_sample, (unsigned long)info->data_size);
    return ESP_OK;
}

static esp_err_t play_wav(FILE *file, const wav_info_t *info)
{
    ESP_RETURN_ON_ERROR(bsp_speaker_set_sample_rate(info->sample_rate), TAG,
                        "Failed to set TF playback sample rate");
    ESP_RETURN_ON_ERROR(bsp_speaker_start(), TAG,
                        "Failed to start TF speaker output");

    while (!s_stop_requested) {
        ESP_RETURN_ON_FALSE(fseek(file, info->data_offset, SEEK_SET) == 0,
                            ESP_FAIL, TAG, "Failed to rewind TF music");
        uint32_t remaining = info->data_size;

        while (remaining > 0 && !s_stop_requested) {
            size_t sample_count = remaining / sizeof(int16_t);
            if (sample_count > TF_INPUT_SAMPLES) {
                sample_count = TF_INPUT_SAMPLES;
            }
            if (info->channels == 2 && (sample_count & 1U) != 0U) {
                --sample_count;
            }
            ESP_RETURN_ON_FALSE(sample_count > 0, ESP_ERR_INVALID_SIZE, TAG,
                                "Invalid WAV data length");

            const size_t samples_read =
                fread(s_input_samples, sizeof(int16_t), sample_count, file);
            if (samples_read == 0) {
                ESP_RETURN_ON_FALSE(!ferror(file), ESP_FAIL, TAG,
                                    "TF music read failed");
                break;
            }
            remaining -= (uint32_t)(samples_read * sizeof(int16_t));

            if (info->channels == 1) {
                for (size_t i = 0; i < samples_read; ++i) {
                    s_stereo_samples[i * 2U] = s_input_samples[i];
                    s_stereo_samples[i * 2U + 1U] = s_input_samples[i];
                }
                ESP_RETURN_ON_ERROR(
                    bsp_speaker_write(s_stereo_samples, samples_read * 2U,
                                      bsp_volume_get()),
                    TAG, "Failed to play mono TF samples");
            } else {
                ESP_RETURN_ON_ERROR(
                    bsp_speaker_write(s_input_samples, samples_read,
                                      bsp_volume_get()),
                    TAG, "Failed to play stereo TF samples");
            }
        }

        if (!s_stop_requested) {
            ESP_LOGI(TAG, "TF music reached EOF; restarting");
        }
    }
    return ESP_OK;
}

static void tf_player_task(void *argument)
{
    (void)argument;
    esp_err_t result = bsp_sd_mount();
    FILE *file = NULL;

    if (result == ESP_OK) {
        bsp_sd_print_info();
        file = fopen(TF_MUSIC_FILE, "rb");
        if (file == NULL) {
            ESP_LOGE(TAG, "Open %s failed; copy a PCM WAV file to TF root",
                     TF_MUSIC_FILE);
            result = ESP_ERR_NOT_FOUND;
        }
    }

    wav_info_t info = {0};
    if (result == ESP_OK) {
        result = parse_wav_header(file, &info);
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "TF playback started: %s", TF_MUSIC_FILE);
        result = play_wav(file, &info);
    }

    if (file != NULL) {
        fclose(file);
    }
    (void)bsp_speaker_stop();
    bsp_sd_unmount();

    if (result != ESP_OK && !s_stop_requested) {
        ESP_LOGE(TAG, "TF playback failed: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "TF playback stopped");
    }

    s_tf_player_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bsp_tf_player_start(void)
{
    if (s_tf_player_task != NULL) {
        return ESP_OK;
    }

    s_stop_requested = false;
    const BaseType_t created =
        xTaskCreatePinnedToCore(tf_player_task, "tf_player",
                                TF_PLAYER_STACK_SIZE, NULL,
                                TF_PLAYER_TASK_PRIORITY, &s_tf_player_task,
                                TF_PLAYER_TASK_CORE);
    if (created != pdPASS) {
        s_tf_player_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void bsp_tf_player_stop(void)
{
    s_stop_requested = true;
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(TF_STOP_WAIT_MS);
    while (s_tf_player_task != NULL &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
