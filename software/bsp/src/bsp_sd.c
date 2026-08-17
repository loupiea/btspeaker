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
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define BSP_SD_MOUNT_POINT "/sdcard"
#define BSP_SD_TEST_FILE   BSP_SD_MOUNT_POINT "/btspeaker_test.txt"
#define BSP_SD_MUSIC_FILE   BSP_SD_MOUNT_POINT "/MUSIC.MP3"

#define SD_CMD0_PROBE_DUMMY_BYTES 10
#define SD_CMD0_PROBE_READ_BYTES  16
#define SD_CMD0_PROBE_CLOCK_HZ    400000
#define SD_CMD0_BITBANG_HALF_PERIOD_US 10
#define BSP_SD_MP3_READ_BUFFER_SIZE     4096
#define BSP_SD_MP3_READ_TEST_MAX_BYTES  (128U * 1024U)
#define BSP_SD_MP3_HEADER_BYTES         16

static const char *TAG = "bsp_sd";
static sdmmc_card_t *s_card;
static bool s_bus_initialized;
static uint8_t s_mp3_read_buffer[BSP_SD_MP3_READ_BUFFER_SIZE];

static esp_err_t configure_chip_selects(void);

static esp_err_t sd_probe_transfer_byte(spi_device_handle_t device,
                                        uint8_t tx_byte,
                                        uint8_t *rx_byte)
{
    uint8_t received = 0xFF;
    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &tx_byte,
        .rx_buffer = &received,
    };

    esp_err_t error = spi_device_polling_transmit(device, &transaction);
    if ((error == ESP_OK) && (rx_byte != NULL)) {
        *rx_byte = received;
    }
    return error;
}

static uint8_t sd_bitbang_transfer_byte(uint8_t tx_byte)
{
    uint8_t rx_byte = 0;

    for (uint8_t bit = 0; bit < 8; bit++) {
        const int tx_level = (tx_byte & 0x80U) != 0 ? 1 : 0;
        gpio_set_level(BSP_SPI_MOSI_GPIO, tx_level);
        esp_rom_delay_us(SD_CMD0_BITBANG_HALF_PERIOD_US);

        gpio_set_level(BSP_SPI_SCLK_GPIO, 1);
        esp_rom_delay_us(SD_CMD0_BITBANG_HALF_PERIOD_US);

        rx_byte <<= 1;
        if (gpio_get_level(BSP_SPI_MISO_GPIO) != 0) {
            rx_byte |= 0x01U;
        }

        gpio_set_level(BSP_SPI_SCLK_GPIO, 0);
        esp_rom_delay_us(SD_CMD0_BITBANG_HALF_PERIOD_US);
        tx_byte <<= 1;
    }

    return rx_byte;
}

static esp_err_t run_cmd0_bitbang_probe(uint8_t *r1, bool *found_response)
{
    if (r1 != NULL) {
        *r1 = 0xFF;
    }
    if (found_response != NULL) {
        *found_response = false;
    }

    ESP_LOGI(TAG, "SD CMD0 bitbang probe: GPIO mode, slow clock, CS active-low");

    ESP_RETURN_ON_ERROR(configure_chip_selects(), TAG,
                        "SD CMD0 bitbang probe failed to configure CS pins");
    ESP_RETURN_ON_ERROR(gpio_set_direction(BSP_SPI_MOSI_GPIO, GPIO_MODE_OUTPUT),
                        TAG, "Failed to set MOSI output");
    ESP_RETURN_ON_ERROR(gpio_set_direction(BSP_SPI_SCLK_GPIO, GPIO_MODE_OUTPUT),
                        TAG, "Failed to set SCLK output");
    ESP_RETURN_ON_ERROR(gpio_set_direction(BSP_SPI_MISO_GPIO, GPIO_MODE_INPUT),
                        TAG, "Failed to set MISO input");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(BSP_SPI_MISO_GPIO, GPIO_PULLUP_ONLY),
                        TAG, "Failed to pull up MISO");

    gpio_set_level(BSP_CH376S_CS_GPIO, 1);
    gpio_set_level(BSP_SD_CS_GPIO, 1);
    gpio_set_level(BSP_SPI_MOSI_GPIO, 1);
    gpio_set_level(BSP_SPI_SCLK_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_LOGI(TAG, "SD CMD0 bitbang probe: sending 80 dummy clocks with CS high");
    for (uint8_t i = 0; i < SD_CMD0_PROBE_DUMMY_BYTES; i++) {
        (void)sd_bitbang_transfer_byte(0xFF);
    }

    gpio_set_level(BSP_SD_CS_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    const uint8_t cmd0[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
    for (uint8_t i = 0; i < sizeof(cmd0); i++) {
        (void)sd_bitbang_transfer_byte(cmd0[i]);
    }

    bool local_found_response = false;
    uint8_t local_r1 = 0xFF;
    for (uint8_t i = 0; i < SD_CMD0_PROBE_READ_BYTES; i++) {
        const uint8_t rx = sd_bitbang_transfer_byte(0xFF);
        ESP_LOGI(TAG, "SD CMD0 bitbang rx[%u]=0x%02X", (unsigned int)i, rx);
        if (!local_found_response && (rx != 0xFF)) {
            local_r1 = rx;
            local_found_response = true;
        }
    }

    gpio_set_level(BSP_SD_CS_GPIO, 1);
    (void)sd_bitbang_transfer_byte(0xFF);

    if (r1 != NULL) {
        *r1 = local_r1;
    }
    if (found_response != NULL) {
        *found_response = local_found_response;
    }

    ESP_LOGI(TAG, "SD CMD0 bitbang probe result: R1=0x%02X", local_r1);
    if (!local_found_response) {
        ESP_LOGW(TAG, "SD CMD0 bitbang probe got no response; MISO stayed 0xFF");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t run_cmd0_probe_sequence(spi_device_handle_t device,
                                         bool active_low,
                                         const char *label,
                                         uint8_t *r1,
                                         bool *found_response)
{
    const int inactive_level = active_low ? 1 : 0;
    const int active_level = active_low ? 0 : 1;
    esp_err_t error;

    if (r1 != NULL) {
        *r1 = 0xFF;
    }
    if (found_response != NULL) {
        *found_response = false;
    }

    gpio_set_level(BSP_SD_CS_GPIO, inactive_level);
    gpio_set_level(BSP_CH376S_CS_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    ESP_LOGI(TAG, "SD CMD0 probe: sending 80 dummy clocks");
    for (uint8_t i = 0; i < SD_CMD0_PROBE_DUMMY_BYTES; i++) {
        error = sd_probe_transfer_byte(device, 0xFF, NULL);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "SD CMD0 probe dummy clock failed: %s",
                     esp_err_to_name(error));
            return error;
        }
    }

    gpio_set_level(BSP_SD_CS_GPIO, active_level);
    vTaskDelay(pdMS_TO_TICKS(1));

    const uint8_t cmd0[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
    for (uint8_t i = 0; i < sizeof(cmd0); i++) {
        error = sd_probe_transfer_byte(device, cmd0[i], NULL);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "SD CMD0 probe command byte failed: %s",
                     esp_err_to_name(error));
            gpio_set_level(BSP_SD_CS_GPIO, inactive_level);
            return error;
        }
    }

    bool local_found_response = false;
    uint8_t local_r1 = 0xFF;
    for (uint8_t i = 0; i < SD_CMD0_PROBE_READ_BYTES; i++) {
        uint8_t rx = 0xFF;
        error = sd_probe_transfer_byte(device, 0xFF, &rx);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "SD CMD0 probe read failed: %s",
                     esp_err_to_name(error));
            gpio_set_level(BSP_SD_CS_GPIO, inactive_level);
            return error;
        }

        ESP_LOGI(TAG, "SD CMD0 probe rx[%u]=0x%02X", (unsigned int)i, rx);
        if (!local_found_response && (rx != 0xFF)) {
            local_r1 = rx;
            local_found_response = true;
        }
    }

    gpio_set_level(BSP_SD_CS_GPIO, inactive_level);
    (void)sd_probe_transfer_byte(device, 0xFF, NULL);

    if (r1 != NULL) {
        *r1 = local_r1;
    }
    if (found_response != NULL) {
        *found_response = local_found_response;
    }

    ESP_LOGI(TAG, "SD CMD0 %s result: R1=0x%02X", label, local_r1);
    return ESP_OK;
}

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

    ESP_LOGI(TAG, "Running SD CMD0 pre-mount probe");
    error = bsp_sd_run_cmd0_probe();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "SD CMD0 hardware-SPI pre-mount probe failed: %s",
                 esp_err_to_name(error));

        uint8_t bitbang_r1 = 0xFF;
        bool bitbang_found_response = false;
        const esp_err_t bitbang_error =
            run_cmd0_bitbang_probe(&bitbang_r1, &bitbang_found_response);
        if (bitbang_error != ESP_OK) {
            ESP_LOGE(TAG, "SD CMD0 pre-mount probe failed: hw-spi=%s, bitbang=%s",
                     esp_err_to_name(error), esp_err_to_name(bitbang_error));
            return bitbang_error;
        }

        ESP_LOGW(TAG,
                 "SD CMD0 bitbang probe responded after hardware-SPI probe failed; continuing to mount for comparison");
    }

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

static void log_mp3_header(const uint8_t *data, size_t length)
{
    const uint8_t b0 = length > 0 ? data[0] : 0;
    const uint8_t b1 = length > 1 ? data[1] : 0;
    const uint8_t b2 = length > 2 ? data[2] : 0;
    const uint8_t b3 = length > 3 ? data[3] : 0;
    const uint8_t b4 = length > 4 ? data[4] : 0;
    const uint8_t b5 = length > 5 ? data[5] : 0;
    const uint8_t b6 = length > 6 ? data[6] : 0;
    const uint8_t b7 = length > 7 ? data[7] : 0;
    const uint8_t b8 = length > 8 ? data[8] : 0;
    const uint8_t b9 = length > 9 ? data[9] : 0;
    const uint8_t b10 = length > 10 ? data[10] : 0;
    const uint8_t b11 = length > 11 ? data[11] : 0;
    const uint8_t b12 = length > 12 ? data[12] : 0;
    const uint8_t b13 = length > 13 ? data[13] : 0;
    const uint8_t b14 = length > 14 ? data[14] : 0;
    const uint8_t b15 = length > 15 ? data[15] : 0;

    ESP_LOGI(TAG,
             "TF MP3 header: bytes=%u, first16=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             (unsigned)length, b0, b1, b2, b3, b4, b5, b6, b7, b8, b9,
             b10, b11, b12, b13, b14, b15);
}

esp_err_t bsp_sd_run_mp3_read_test(void)
{
    if (s_card == NULL) {
        ESP_LOGE(TAG, "TF MP3 read test requires mounted SD card");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(BSP_SD_MUSIC_FILE, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s: %s", BSP_SD_MUSIC_FILE,
                 strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TF MP3 read test started: file=%s, max=%u bytes",
             BSP_SD_MUSIC_FILE, (unsigned)BSP_SD_MP3_READ_TEST_MAX_BYTES);

    uint32_t total_bytes = 0;
    bool header_logged = false;
    const TickType_t start_tick = xTaskGetTickCount();

    while (total_bytes < BSP_SD_MP3_READ_TEST_MAX_BYTES) {
        const size_t remaining = BSP_SD_MP3_READ_TEST_MAX_BYTES - total_bytes;
        const size_t request = remaining > sizeof(s_mp3_read_buffer)
                                   ? sizeof(s_mp3_read_buffer)
                                   : remaining;
        const size_t bytes_read = fread(s_mp3_read_buffer, 1, request, file);
        if (bytes_read == 0) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "TF MP3 read failed after %" PRIu32 " bytes",
                         total_bytes);
                fclose(file);
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "TF MP3 read test reached EOF");
            break;
        }

        if (!header_logged) {
            const size_t header_bytes = bytes_read > BSP_SD_MP3_HEADER_BYTES
                                            ? BSP_SD_MP3_HEADER_BYTES
                                            : bytes_read;
            log_mp3_header(s_mp3_read_buffer, header_bytes);
            header_logged = true;
        }

        total_bytes += (uint32_t)bytes_read;
    }

    fclose(file);

    const TickType_t elapsed_ticks = xTaskGetTickCount() - start_tick;
    const uint32_t elapsed_ms =
        elapsed_ticks == 0 ? 1U : elapsed_ticks * portTICK_PERIOD_MS;
    const uint32_t bytes_per_second =
        (uint32_t)(((uint64_t)total_bytes * 1000ULL) / elapsed_ms);

    ESP_LOGI(TAG,
             "TF MP3 read speed: bytes=%" PRIu32 ", time_ms=%" PRIu32
             ", rate=%" PRIu32 " B/s",
             total_bytes, elapsed_ms, bytes_per_second);
    return ESP_OK;
}

esp_err_t bsp_sd_run_cmd0_probe(void)
{
    if (s_card != NULL) {
        ESP_LOGW(TAG, "SD CMD0 probe skipped because card is already mounted");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = configure_chip_selects();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD CMD0 probe failed to configure CS pins: %s",
                 esp_err_to_name(error));
        return error;
    }

    gpio_set_direction(BSP_SPI_MISO_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BSP_SPI_MISO_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_level(BSP_SD_CS_GPIO, 1);
    gpio_set_level(BSP_CH376S_CS_GPIO, 1);

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_SPI_MOSI_GPIO,
        .miso_io_num = BSP_SPI_MISO_GPIO,
        .sclk_io_num = BSP_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1,
    };

    error = spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_DISABLED);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD CMD0 probe failed to initialize SPI bus: %s",
                 esp_err_to_name(error));
        return error;
    }

    spi_device_handle_t device = NULL;
    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = SD_CMD0_PROBE_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };

    error = spi_bus_add_device(SPI3_HOST, &device_config, &device);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD CMD0 probe failed to add SPI device: %s",
                 esp_err_to_name(error));
        spi_bus_free(SPI3_HOST);
        return error;
    }

    ESP_LOGI(TAG, "SD CS is active-low in SPI mode; inactive=1, active=0");

    uint8_t r1 = 0xFF;
    bool found_response = false;
    const bool active_low = true;
    error = run_cmd0_probe_sequence(device,
                                    active_low,
                                    "active-low probe",
                                    &r1,
                                    &found_response);
    if (error != ESP_OK) {
        spi_bus_remove_device(device);
        spi_bus_free(SPI3_HOST);
        return error;
    }
    ESP_LOGI(TAG, "SD CMD0 active-low probe result copied: R1=0x%02X", r1);

    uint8_t active_high_r1 = 0xFF;
    bool active_high_found_response = false;
    if (!found_response) {
        const bool active_high_check = false;
        ESP_LOGW(TAG, "SD CMD0 active-low probe got no response; running active-high check");
        error = run_cmd0_probe_sequence(device,
                                        active_high_check,
                                        "active-high check",
                                        &active_high_r1,
                                        &active_high_found_response);
        if (error != ESP_OK) {
            spi_bus_remove_device(device);
            spi_bus_free(SPI3_HOST);
            return error;
        }
        ESP_LOGI(TAG, "SD CMD0 active-high check result copied: R1=0x%02X",
                 active_high_r1);
    } else {
        ESP_LOGI(TAG, "SD CMD0 active-high check result: skipped");
    }

    spi_bus_remove_device(device);
    spi_bus_free(SPI3_HOST);

    ESP_LOGI(TAG, "SD CMD0 probe result: R1=0x%02X", r1);
    if (!found_response) {
        if (active_high_found_response) {
            ESP_LOGE(TAG,
                     "SD CMD0 responded only during active-high check; CS polarity or wiring is likely inverted");
            return ESP_ERR_INVALID_RESPONSE;
        }

        ESP_LOGW(TAG,
                 "SD CMD0 probe got no response in active-low or active-high check; MISO stayed 0xFF");
        return ESP_ERR_TIMEOUT;
    }
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
