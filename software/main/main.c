#include "bsp_board.h"
#include "bsp_sd.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    bsp_board_init();
    bsp_board_print_info();

    const esp_err_t mount_result = bsp_sd_mount();
    if (mount_result == ESP_OK) {
        bsp_sd_print_info();
        if (bsp_sd_list_root() != ESP_OK) {
            ESP_LOGW(TAG, "SD directory test failed");
        }
        if (bsp_sd_run_file_test() != ESP_OK) {
            ESP_LOGE(TAG, "SD write/read test failed");
        }
    } else {
        ESP_LOGE(TAG, "SD test stopped because mount failed: %s",
                 esp_err_to_name(mount_result));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
