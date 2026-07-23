#include "bsp_board.h"
#include "bsp_bluetooth.h"
#include "bsp_ch376.h"
#include "bsp_input.h"
#include "bsp_mode.h"
#include "bsp_oled.h"
#include "bsp_speaker.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

static void display_mode(bsp_mode_t mode)
{
    esp_err_t result = bsp_oled_show_lines("Mode:", bsp_mode_name(mode));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "OLED mode display failed: %s", esp_err_to_name(result));
    }
}

static void enter_mode(bsp_mode_t mode)
{
    ESP_LOGI(TAG, "Source mode: %s", bsp_mode_name(mode));
    display_mode(mode);

    if (mode == BSP_MODE_BLUETOOTH) {
        esp_err_t bluetooth_result = bsp_bluetooth_a2dp_sink_start();
        if (bluetooth_result != ESP_OK) {
            ESP_LOGE(TAG, "Bluetooth A2DP start failed: %s",
                     esp_err_to_name(bluetooth_result));
        } else {
            ESP_LOGI(TAG, "Bluetooth is discoverable; connect to BT Speaker");
        }
    } else if (mode == BSP_MODE_USB) {
        ESP_LOGI(TAG, "U Disk mode selected; playback will be added after USB file read test");
    } else if (mode == BSP_MODE_AUX) {
        ESP_LOGI(TAG, "AUX mode selected; audio path waits for MCLK routing on next board");
    }
}

void app_main(void)
{
    bsp_board_init();
    bsp_board_print_info();

    esp_err_t oled_result = bsp_oled_run_self_test();
    if (oled_result != ESP_OK) {
        ESP_LOGE(TAG, "OLED self-test failed: %s", esp_err_to_name(oled_result));
    }

    esp_err_t speaker_result = bsp_speaker_run_self_test();
    if (speaker_result != ESP_OK) {
        ESP_LOGE(TAG, "Speaker self-test failed: %s",
                 esp_err_to_name(speaker_result));
    }

    uint8_t response = 0;
    uint8_t version = 0;

    esp_err_t result = bsp_ch376_init();
    if (result == ESP_OK) {
        result = bsp_ch376_check_exist(0x65, &response);
    }
    if (result == ESP_OK) {
        result = bsp_ch376_get_version(&version);
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "CH376S self-test passed; version raw=0x%02X", version);
    } else {
        ESP_LOGE(TAG, "CH376S self-test failed: %s, response=0x%02X",
                 esp_err_to_name(result), response);
    }

    esp_err_t input_result = bsp_input_init();
    if (input_result != ESP_OK) {
        ESP_LOGE(TAG, "Input init failed: %s", esp_err_to_name(input_result));
    }

    bsp_mode_t current_mode = BSP_MODE_BLUETOOTH;
    enter_mode(current_mode);

    while (1) {
        bsp_input_event_t input_event = {0};
        bsp_input_poll(&input_event);

        if (input_event.source_pressed) {
            current_mode = bsp_mode_next(current_mode);
            enter_mode(current_mode);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
