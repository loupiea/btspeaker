#include "bsp_bluetooth.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#include "bsp_speaker.h"
#include "bsp_volume.h"
#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "bsp_bluetooth";

static const char BT_SPEAKER_DEVICE_NAME[] = "BT Speaker";
static bool s_bt_started;
static volatile bool s_playback_enabled;

static esp_err_t init_nvs_for_bluetooth(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
        result = nvs_flash_init();
    }
    return result;
}

static uint32_t sample_rate_from_sbc_config(const esp_a2d_mcc_t *codec)
{
    if (codec == NULL || codec->type != ESP_A2D_MCT_SBC) {
        return 44100;
    }

    const uint8_t oct0 = codec->cie.sbc[0];
    if ((oct0 & (1U << 6)) != 0U) {
        return 32000;
    }
    if ((oct0 & (1U << 5)) != 0U) {
        return 44100;
    }
    if ((oct0 & (1U << 4)) != 0U) {
        return 48000;
    }
    return 16000;
}

static void a2dp_data_callback(const uint8_t *data, uint32_t len)
{
    if (!s_playback_enabled || data == NULL || len == 0U) {
        return;
    }

    const size_t sample_count = len / sizeof(int16_t);
    esp_err_t result =
        bsp_speaker_write((const int16_t *)data, sample_count,
                          bsp_volume_get());
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write Bluetooth PCM: %s",
                 esp_err_to_name(result));
    }
}

static void a2dp_event_callback(esp_a2d_cb_event_t event,
                                esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "A2DP connection state: %d",
                 param->conn_stat.state);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
        break;

    case ESP_A2D_AUDIO_CFG_EVT: {
        if (!s_playback_enabled) {
            ESP_LOGD(TAG, "Ignoring A2DP audio config outside Bluetooth mode");
            break;
        }
        const uint32_t sample_rate_hz =
            sample_rate_from_sbc_config(&param->audio_cfg.mcc);
        esp_err_t result = bsp_speaker_set_sample_rate(sample_rate_hz);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "A2DP SBC sample rate: %" PRIu32 " Hz",
                     sample_rate_hz);
        } else {
            ESP_LOGE(TAG, "Failed to apply A2DP sample rate: %s",
                     esp_err_to_name(result));
        }
        break;
    }

    case ESP_A2D_PROF_STATE_EVT:
        ESP_LOGI(TAG, "A2DP profile state: %d",
                 param->a2d_prof_stat.init_state);
        break;

    default:
        ESP_LOGD(TAG, "A2DP event: %d", event);
        break;
    }
}

static void gap_event_callback(esp_bt_gap_cb_event_t event,
                               esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Bluetooth authentication %s",
                 param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? "OK"
                                                                : "failed");
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "Bluetooth scan mode changed: %d",
                 param->mode_chg.mode);
        break;

    default:
        ESP_LOGD(TAG, "GAP event: %d", event);
        break;
    }
}

esp_err_t bsp_bluetooth_a2dp_sink_start(void)
{
    if (s_bt_started) {
        s_playback_enabled = true;
        (void)esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                      ESP_BT_GENERAL_DISCOVERABLE);
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_nvs_for_bluetooth(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(bsp_speaker_init(), TAG, "Speaker init failed");

    ESP_RETURN_ON_ERROR(esp_bt_controller_mem_release(ESP_BT_MODE_BLE), TAG,
                        "Failed to release BLE memory");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG,
                        "BT controller init failed");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT), TAG,
                        "BT controller enable failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_init(), TAG, "Bluedroid init failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG,
                        "Bluedroid enable failed");

    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    ESP_RETURN_ON_ERROR(
        esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin_code), TAG,
        "Failed to set BT pin");
    ESP_RETURN_ON_ERROR(esp_bt_gap_register_callback(gap_event_callback), TAG,
                        "Failed to register GAP callback");
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_device_name(BT_SPEAKER_DEVICE_NAME),
                        TAG, "Failed to set BT name");

    ESP_RETURN_ON_ERROR(esp_a2d_register_callback(a2dp_event_callback), TAG,
                        "Failed to register A2DP callback");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_register_data_callback(a2dp_data_callback),
                        TAG, "Failed to register A2DP data callback");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_init(), TAG, "A2DP sink init failed");

    ESP_RETURN_ON_ERROR(
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                 ESP_BT_GENERAL_DISCOVERABLE),
        TAG, "Failed to make BT discoverable");

    s_bt_started = true;
    s_playback_enabled = true;
    ESP_LOGI(TAG, "Bluetooth A2DP sink ready: name=\"%s\", volume=%u/50",
             BT_SPEAKER_DEVICE_NAME, (unsigned int)bsp_volume_get());
    return ESP_OK;
}

void bsp_bluetooth_a2dp_sink_stop(void)
{
    s_playback_enabled = false;
    if (s_bt_started) {
        (void)esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE,
                                      ESP_BT_NON_DISCOVERABLE);
    }
    (void)bsp_speaker_stop();
}
