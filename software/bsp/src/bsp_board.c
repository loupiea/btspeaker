#include <inttypes.h>

#include "bsp_board.h"
#include "bsp_pins.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"

static const char *TAG = "bsp_board";

void bsp_board_init(void)
{
    /* Keep the amplifiers muted until the I2S output is ready. */
    const gpio_config_t amp_sd_config = {
        .pin_bit_mask = 1ULL << BSP_AMP_SD_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&amp_sd_config));
    ESP_ERROR_CHECK(gpio_set_level(BSP_AMP_SD_GPIO, 0));
    ESP_LOGI(TAG, "BSP initialized; amplifiers are muted");
}

void bsp_board_print_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "BT Speaker board self-test");
    ESP_LOGI(TAG, "Chip cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Chip revision: v%d.%d",
             chip_info.revision / 100,
             chip_info.revision % 100);

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash size: %" PRIu32 " MB", flash_size / (1024U * 1024U));
    } else {
        ESP_LOGW(TAG, "Failed to read flash size");
    }
}
