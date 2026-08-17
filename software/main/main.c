#include "bsp_aux.h"
#include "bsp_board.h"
#include "bsp_bluetooth.h"
#include "bsp_input.h"
#include "bsp_mode.h"
#include "bsp_oled.h"
#include "bsp_tf_player.h"
#include "bsp_volume.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

static void display_status(bsp_mode_t mode)
{
    char volume_line[24];
    snprintf(volume_line, sizeof(volume_line), "%s VOL %u/%u",
             bsp_mode_name(mode), (unsigned int)bsp_volume_get(),
             (unsigned int)BSP_VOLUME_MAX);

    esp_err_t result = bsp_oled_show_lines("Mode:", volume_line);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "OLED status display failed: %s", esp_err_to_name(result));
    }
}

static void enter_mode(bsp_mode_t mode)
{
    ESP_LOGI(TAG, "Source mode: %s", bsp_mode_name(mode));
    display_status(mode);

    if (mode == BSP_MODE_BLUETOOTH) {
        esp_err_t bluetooth_result = bsp_bluetooth_a2dp_sink_start();
        if (bluetooth_result != ESP_OK) {
            ESP_LOGE(TAG, "Bluetooth A2DP start failed: %s",
                     esp_err_to_name(bluetooth_result));
        } else {
            ESP_LOGI(TAG, "Bluetooth is discoverable; connect to BT Speaker");
        }
    } else if (mode == BSP_MODE_TF) {
        esp_err_t tf_result = bsp_tf_player_start();
        if (tf_result != ESP_OK) {
            ESP_LOGE(TAG, "TF playback start failed: %s",
                     esp_err_to_name(tf_result));
        }
    } else if (mode == BSP_MODE_AUX) {
        esp_err_t aux_result = bsp_aux_start();
        if (aux_result != ESP_OK) {
            ESP_LOGE(TAG, "AUX playback start failed: %s",
                     esp_err_to_name(aux_result));
        }
    }
}

static void leave_mode(bsp_mode_t previous_mode)
{
    if (previous_mode == BSP_MODE_BLUETOOTH) {
        bsp_bluetooth_a2dp_sink_stop();
    } else if (previous_mode == BSP_MODE_TF) {
        bsp_tf_player_stop();
    } else if (previous_mode == BSP_MODE_AUX) {
        bsp_aux_stop();
    }
}

void app_main(void)
{
    bsp_board_init();
    bsp_board_print_info();

    esp_err_t input_result = bsp_input_init();
    if (input_result != ESP_OK) {
        ESP_LOGE(TAG, "Input init failed: %s", esp_err_to_name(input_result));
    }

    bsp_mode_t current_mode = BSP_MODE_BLUETOOTH;
    enter_mode(current_mode);

    while (1) {
        bsp_input_event_t input_event = {0};
        bsp_input_poll(&input_event);

        if (input_event.encoder_delta != 0) {
            bsp_volume_adjust(input_event.encoder_delta);
            ESP_LOGI(TAG, "Volume: %u/%u", (unsigned int)bsp_volume_get(),
                     (unsigned int)BSP_VOLUME_MAX);
            display_status(current_mode);
        }

        if (input_event.source_pressed) {
            leave_mode(current_mode);
            current_mode = bsp_mode_next(current_mode);
            enter_mode(current_mode);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
