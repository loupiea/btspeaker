#include "bsp_sd.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define BSP_SD_MOUNT_POINT "/sdcard"
#define BSP_SD_TEST_FILE   BSP_SD_MOUNT_POINT "/btspeaker_test.txt"

static const char *TAG = "bsp_sd";
static sdmmc_card_t *s_card;
static bool s_bus_initialized;

static esp_err_t configure_chip_selects(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BSP_SD_CS_GPIO) |
                        (1ULL << BSP_CH376S_CS_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }

    error = gpio_set_level(BSP_SD_CS_GPIO, 1);
    if (error != ESP_OK) {
        return error;
    }
    return gpio_set_level(BSP_CH376S_CS_GPIO, 1);
}

static void log_spi_gpio_self_test(void)
{
    gpio_set_direction(BSP_SPI_MOSI_GPIO, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(BSP_SPI_SCLK_GPIO, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_pull_mode(BSP_SPI_MOSI_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BSP_SPI_SCLK_GPIO, GPIO_FLOATING);

    gpio_set_level(BSP_SD_CS_GPIO, 0);
    gpio_set_level(BSP_SPI_MOSI_GPIO, 0);
    gpio_set_level(BSP_SPI_SCLK_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));

    const int cs_low = gpio_get_level(BSP_SD_CS_GPIO);
    const int mosi_low = gpio_get_level(BSP_SPI_MOSI_GPIO);
    const int sclk_low = gpio_get_level(BSP_SPI_SCLK_GPIO);

    gpio_set_level(BSP_SD_CS_GPIO, 1);
    gpio_set_level(BSP_SPI_MOSI_GPIO, 1);
    gpio_set_level(BSP_SPI_SCLK_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    const int cs_high = gpio_get_level(BSP_SD_CS_GPIO);
    const int mosi_high = gpio_get_level(BSP_SPI_MOSI_GPIO);
    const int sclk_high = gpio_get_level(BSP_SPI_SCLK_GPIO);

    gpio_set_level(BSP_SD_CS_GPIO, 1);
    gpio_set_level(BSP_CH376S_CS_GPIO, 1);
    gpio_set_level(BSP_SPI_MOSI_GPIO, 1);
    gpio_set_level(BSP_SPI_SCLK_GPIO, 0);

    ESP_LOGI(TAG, "GPIO self-test low/high: CS=%d/%d, MOSI=%d/%d, SCLK=%d/%d, MISO=%d",
             cs_low,
             cs_high,
             mosi_low,
             mosi_high,
             sclk_low,
             sclk_high,
             gpio_get_level(BSP_SPI_MISO_GPIO));
}

esp_err_t bsp_sd_mount(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    esp_log_level_set("sdspi_host", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_init", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_sd", ESP_LOG_DEBUG);

    esp_err_t error = configure_chip_selects();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SPI chip selects: %s", esp_err_to_name(error));
        return error;
    }

    gpio_set_direction(BSP_SPI_MISO_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BSP_SPI_MISO_GPIO, GPIO_PULLUP_ONLY);
    log_spi_gpio_self_test();
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Idle levels before SPI init: CS=%d, MISO=%d",
             gpio_get_level(BSP_SD_CS_GPIO),
             gpio_get_level(BSP_SPI_MISO_GPIO));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_SPI_MOSI_GPIO,
        .miso_io_num = BSP_SPI_MISO_GPIO,
        .sclk_io_num = BSP_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };

    error = spi_bus_initialize(host.slot, &bus_config, SDSPI_DEFAULT_DMA);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(error));
        return error;
    }
    s_bus_initialized = true;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = BSP_SD_CS_GPIO;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Mounting SD card: SCLK=%d, MISO=%d, MOSI=%d, CS=%d",
             BSP_SPI_SCLK_GPIO,
             BSP_SPI_MISO_GPIO,
             BSP_SPI_MOSI_GPIO,
             BSP_SD_CS_GPIO);

    error = esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT,
                                    &host,
                                    &slot_config,
                                    &mount_config,
                                    &s_card);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(error));
        spi_bus_free(host.slot);
        s_bus_initialized = false;
        s_card = NULL;
        return error;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);
    return ESP_OK;
}

void bsp_sd_print_info(void)
{
    if (s_card == NULL) {
        ESP_LOGW(TAG, "SD card is not mounted");
        return;
    }

    sdmmc_card_print_info(stdout, s_card);
}

esp_err_t bsp_sd_list_root(void)
{
    DIR *directory = opendir(BSP_SD_MOUNT_POINT);
    if (directory == NULL) {
        ESP_LOGE(TAG, "Failed to open SD root: %s", strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SD root directory:");
    struct dirent *entry;
    unsigned int count = 0;
    while ((entry = readdir(directory)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
        count++;
    }
    closedir(directory);

    if (count == 0) {
        ESP_LOGI(TAG, "  <empty>");
    }
    return ESP_OK;
}

esp_err_t bsp_sd_run_file_test(void)
{
    static const char test_text[] = "BT speaker SD card test OK\n";
    char read_buffer[sizeof(test_text)] = {0};

    FILE *file = fopen(BSP_SD_TEST_FILE, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to create %s: %s", BSP_SD_TEST_FILE, strerror(errno));
        return ESP_FAIL;
    }

    const size_t written = fwrite(test_text, 1, sizeof(test_text) - 1U, file);
    const int close_result = fclose(file);
    if ((written != (sizeof(test_text) - 1U)) || (close_result != 0)) {
        ESP_LOGE(TAG, "Failed to write SD test file");
        return ESP_FAIL;
    }

    file = fopen(BSP_SD_TEST_FILE, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to reopen %s: %s", BSP_SD_TEST_FILE, strerror(errno));
        return ESP_FAIL;
    }

    const size_t read_size = fread(read_buffer, 1, sizeof(read_buffer) - 1U, file);
    fclose(file);

    if ((read_size != (sizeof(test_text) - 1U)) || (strcmp(read_buffer, test_text) != 0)) {
        ESP_LOGE(TAG, "SD read-back data mismatch");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SD write/read test passed: %s", BSP_SD_TEST_FILE);
    return ESP_OK;
}

void bsp_sd_unmount(void)
{
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }

    if (s_bus_initialized) {
        spi_bus_free(SPI3_HOST);
        s_bus_initialized = false;
    }
}
