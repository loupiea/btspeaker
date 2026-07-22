#include "bsp_board.h"
#include "bsp_ch376.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    bsp_board_init();
    bsp_board_print_info();

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

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
