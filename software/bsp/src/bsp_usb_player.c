#include "bsp_usb_player.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "bsp_ch376.h"
#include "bsp_speaker.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_usb_player";

enum {
    USB_PLAYER_VOLUME = 5,
    USB_PLAYER_STACK_SIZE = 4096,
    USB_PLAYER_TASK_PRIORITY = 5,
    USB_PLAYER_READ_BUFFER_SIZE = 512,
    WAV_HEADER_PREFIX_SIZE = 12,
    WAV_CHUNK_HEADER_SIZE = 8,
    WAV_FORMAT_PCM = 1,
    WAV_FORMAT_IEEE_FLOAT = 3,
    WAV_FORMAT_EXTENSIBLE = 0xFFFE,
    WAV_BITS_PER_SAMPLE = 16,
    WAV_FLOAT_BITS_PER_SAMPLE = 32,
    WAV_EXTENSIBLE_EXTRA_SIZE = 24,
    WAV_EXTENSIBLE_SUBFORMAT_OFFSET = 8,
};

static const char USB_MUSIC_FILE[] = "/MUSIC.WAV";
static const uint8_t WAV_SUBFORMAT_PCM[16] = {
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x10, 0x00,
    0x80, 0x00,
    0x00, 0xAA,
    0x00, 0x38, 0x9B, 0x71,
};

typedef enum {
    WAV_SAMPLE_FORMAT_PCM16,
    WAV_SAMPLE_FORMAT_FLOAT32,
} wav_sample_format_t;

typedef struct {
    uint16_t channels;
    uint32_t sample_rate_hz;
    uint16_t bits_per_sample;
    uint32_t data_bytes;
    wav_sample_format_t sample_format;
} wav_info_t;

static TaskHandle_t s_usb_player_task;
static volatile bool s_stop_requested;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static esp_err_t read_exact(uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length && !s_stop_requested) {
        size_t bytes_read = 0;
        ESP_RETURN_ON_ERROR(
            bsp_ch376_file_read(buffer + offset, length - offset, &bytes_read),
            TAG, "USB file read failed");
        if (bytes_read == 0) {
            return ESP_ERR_INVALID_SIZE;
        }
        offset += bytes_read;
    }
    return s_stop_requested ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t skip_bytes(uint32_t length)
{
    uint8_t buffer[USB_PLAYER_READ_BUFFER_SIZE];
    uint32_t remaining = length;

    while (remaining > 0 && !s_stop_requested) {
        const size_t request = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        size_t bytes_read = 0;
        ESP_RETURN_ON_ERROR(bsp_ch376_file_read(buffer, request, &bytes_read),
                            TAG, "Failed to skip WAV chunk");
        if (bytes_read == 0) {
            return ESP_ERR_INVALID_SIZE;
        }
        remaining -= bytes_read;
    }
    return s_stop_requested ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t parse_wav_header(wav_info_t *info)
{
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "WAV info is NULL");
    memset(info, 0, sizeof(*info));

    uint8_t prefix[WAV_HEADER_PREFIX_SIZE];
    ESP_RETURN_ON_ERROR(read_exact(prefix, sizeof(prefix)), TAG,
                        "Failed to read WAV prefix");
    ESP_RETURN_ON_FALSE(memcmp(prefix, "RIFF", 4) == 0, ESP_ERR_INVALID_RESPONSE,
                        TAG, "USB file is not RIFF");
    ESP_RETURN_ON_FALSE(memcmp(prefix + 8, "WAVE", 4) == 0,
                        ESP_ERR_INVALID_RESPONSE, TAG, "USB file is not WAVE");

    bool have_fmt = false;
    while (!s_stop_requested) {
        uint8_t chunk[WAV_CHUNK_HEADER_SIZE];
        ESP_RETURN_ON_ERROR(read_exact(chunk, sizeof(chunk)), TAG,
                            "Failed to read WAV chunk header");

        const uint32_t chunk_size = read_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            ESP_RETURN_ON_FALSE(chunk_size >= sizeof(fmt), ESP_ERR_INVALID_RESPONSE,
                                TAG, "WAV fmt chunk too small");
            ESP_RETURN_ON_ERROR(read_exact(fmt, sizeof(fmt)), TAG,
                                "Failed to read WAV fmt chunk");

            const uint16_t audio_format = read_le16(fmt);
            info->channels = read_le16(fmt + 2);
            info->sample_rate_hz = read_le32(fmt + 4);
            info->bits_per_sample = read_le16(fmt + 14);

            uint8_t extensible_extra[WAV_EXTENSIBLE_EXTRA_SIZE] = {0};
            uint32_t extra_bytes = chunk_size - sizeof(fmt);
            const uint32_t extensible_bytes =
                extra_bytes > sizeof(extensible_extra)
                    ? sizeof(extensible_extra)
                    : extra_bytes;
            if (extensible_bytes > 0) {
                ESP_RETURN_ON_ERROR(read_exact(extensible_extra, extensible_bytes),
                                    TAG, "Failed to read WAV fmt extension");
                extra_bytes -= extensible_bytes;
            }

            const bool is_extensible_pcm =
                audio_format == WAV_FORMAT_EXTENSIBLE &&
                extensible_bytes >= WAV_EXTENSIBLE_EXTRA_SIZE &&
                memcmp(extensible_extra + WAV_EXTENSIBLE_SUBFORMAT_OFFSET,
                       WAV_SUBFORMAT_PCM, sizeof(WAV_SUBFORMAT_PCM)) == 0;
            const bool is_float32 =
                audio_format == WAV_FORMAT_IEEE_FLOAT &&
                info->bits_per_sample == WAV_FLOAT_BITS_PER_SAMPLE;
            ESP_LOGI(TAG, "WAV fmt: format=0x%04X, channels=%u, sample_rate=%" PRIu32
                          ", bits=%u",
                     audio_format, info->channels, info->sample_rate_hz,
                     info->bits_per_sample);

            if (audio_format != WAV_FORMAT_PCM && !is_extensible_pcm &&
                !is_float32) {
                ESP_LOGE(TAG, "WAV format unsupported: format=0x%04X", audio_format);
                return ESP_ERR_NOT_SUPPORTED;
            }
            ESP_RETURN_ON_FALSE(info->channels == 1 || info->channels == 2,
                                ESP_ERR_NOT_SUPPORTED, TAG,
                                "Only mono/stereo WAV is supported");
            if (is_float32) {
                info->sample_format = WAV_SAMPLE_FORMAT_FLOAT32;
            } else {
                ESP_RETURN_ON_FALSE(info->bits_per_sample == WAV_BITS_PER_SAMPLE,
                                    ESP_ERR_NOT_SUPPORTED, TAG,
                                    "Only 16-bit PCM WAV is supported");
                info->sample_format = WAV_SAMPLE_FORMAT_PCM16;
            }

            if (extra_bytes > 0) {
                ESP_RETURN_ON_ERROR(skip_bytes(extra_bytes), TAG,
                                    "Failed to skip extra fmt bytes");
            }
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            ESP_RETURN_ON_FALSE(have_fmt, ESP_ERR_INVALID_RESPONSE, TAG,
                                "WAV data chunk appears before fmt chunk");
            info->data_bytes = chunk_size;
            return ESP_OK;
        } else {
            ESP_RETURN_ON_ERROR(skip_bytes(chunk_size), TAG,
                                "Failed to skip unknown WAV chunk");
        }

        if ((chunk_size & 1U) != 0U) {
            ESP_RETURN_ON_ERROR(skip_bytes(1), TAG, "Failed to skip WAV pad byte");
        }
    }

    return ESP_ERR_INVALID_STATE;
}

static esp_err_t write_wav_pcm(const uint8_t *buffer, size_t bytes,
                               uint16_t channels)
{
    ESP_RETURN_ON_FALSE((bytes % sizeof(int16_t)) == 0, ESP_ERR_INVALID_SIZE,
                        TAG, "PCM data is not 16-bit aligned");

    if (channels == 2) {
        return bsp_speaker_write((const int16_t *)buffer,
                                 bytes / sizeof(int16_t),
                                 USB_PLAYER_VOLUME);
    }

    int16_t stereo[USB_PLAYER_READ_BUFFER_SIZE];
    const int16_t *mono = (const int16_t *)buffer;
    const size_t mono_samples = bytes / sizeof(int16_t);
    for (size_t i = 0; i < mono_samples; ++i) {
        stereo[i * 2U] = mono[i];
        stereo[i * 2U + 1U] = mono[i];
    }

    return bsp_speaker_write(stereo, mono_samples * 2U, USB_PLAYER_VOLUME);
}

static int16_t float_to_i16(float sample)
{
    if (sample > 1.0f) {
        sample = 1.0f;
    } else if (sample < -1.0f) {
        sample = -1.0f;
    }
    return (int16_t)(sample * 32767.0f);
}

static float read_le_float32(const uint8_t *data)
{
    uint32_t raw = read_le32(data);
    float value = 0.0f;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static esp_err_t write_wav_float32(const uint8_t *buffer, size_t bytes,
                                   uint16_t channels)
{
    ESP_RETURN_ON_FALSE((bytes % sizeof(float)) == 0, ESP_ERR_INVALID_SIZE,
                        TAG, "Float PCM data is not 32-bit aligned");

    int16_t samples[USB_PLAYER_READ_BUFFER_SIZE / sizeof(float) * 2U];
    const size_t float_samples = bytes / sizeof(float);
    size_t output_samples = 0;
    for (size_t i = 0; i < float_samples; ++i) {
        const int16_t sample = float_to_i16(read_le_float32(buffer + i * sizeof(float)));
        samples[output_samples++] = sample;
        if (channels == 1) {
            samples[output_samples++] = sample;
        }
    }

    return bsp_speaker_write(samples, output_samples, USB_PLAYER_VOLUME);
}

static esp_err_t play_music_file(void)
{
    ESP_RETURN_ON_ERROR(bsp_ch376_usb_disk_mount(), TAG,
                        "USB disk mount failed");

    esp_err_t result = bsp_ch376_file_open(USB_MUSIC_FILE);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Put a PCM 16-bit WAV file at %s", USB_MUSIC_FILE);
        return result;
    }

    wav_info_t wav = {0};
    result = parse_wav_header(&wav);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Playing %s: %" PRIu32 " Hz, %u ch, %u bits, %" PRIu32
                      " data bytes",
                 "MUSIC.WAV", wav.sample_rate_hz, wav.channels,
                 wav.bits_per_sample, wav.data_bytes);
        result = bsp_speaker_set_sample_rate(wav.sample_rate_hz);
    }

    uint8_t buffer[USB_PLAYER_READ_BUFFER_SIZE];
    uint32_t remaining = wav.data_bytes;
    while (result == ESP_OK && remaining > 0 && !s_stop_requested) {
        size_t bytes_read = 0;
        const size_t request = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        result = bsp_ch376_file_read(buffer, request, &bytes_read);
        if (result != ESP_OK || bytes_read == 0) {
            break;
        }

        if (wav.sample_format == WAV_SAMPLE_FORMAT_FLOAT32) {
            const size_t aligned_bytes = bytes_read & ~(sizeof(float) - 1U);
            result = write_wav_float32(buffer, aligned_bytes, wav.channels);
        } else {
            const size_t aligned_bytes = bytes_read & ~(sizeof(int16_t) - 1U);
            result = write_wav_pcm(buffer, aligned_bytes, wav.channels);
        }
        remaining -= bytes_read;
    }

    bsp_ch376_file_close();
    (void)bsp_speaker_stop();
    return result;
}

static void usb_player_task(void *argument)
{
    (void)argument;

    const esp_err_t result = play_music_file();
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "USB playback finished");
    } else if (s_stop_requested) {
        ESP_LOGI(TAG, "USB playback stopped");
    } else {
        ESP_LOGE(TAG, "USB playback failed: %s", esp_err_to_name(result));
    }

    s_usb_player_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bsp_usb_player_start(void)
{
    if (s_usb_player_task != NULL) {
        return ESP_OK;
    }

    s_stop_requested = false;
    BaseType_t created = xTaskCreate(usb_player_task, "usb_player",
                                     USB_PLAYER_STACK_SIZE, NULL,
                                     USB_PLAYER_TASK_PRIORITY,
                                     &s_usb_player_task);
    if (created != pdPASS) {
        s_usb_player_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB player task started, file=%s", USB_MUSIC_FILE);
    return ESP_OK;
}

void bsp_usb_player_stop(void)
{
    s_stop_requested = true;
}
