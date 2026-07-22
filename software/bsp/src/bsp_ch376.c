#include "bsp_ch376.h"

#include <stdbool.h>
#include <stddef.h>

#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "bsp_ch376";

enum {
    CH376_CMD_GET_IC_VER = 0x01,
    CH376_CMD_CHECK_EXIST = 0x06,
    CH376_TSC_DELAY_US = 2,
};

static spi_device_handle_t s_spi_device;
static bool s_bus_initialized;

static esp_err_t transfer_byte(uint8_t tx_byte, uint8_t *rx_byte)
{
    uint8_t received = 0;
    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &tx_byte,
        .rx_buffer = &received,
    };

    const esp_err_t result = spi_device_polling_transmit(s_spi_device, &transaction);
    if (result == ESP_OK && rx_byte != NULL) {
        *rx_byte = received;
    }
    return result;
}

static esp_err_t begin_command(void)
{
    ESP_RETURN_ON_FALSE(s_spi_device != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "CH376S is not initialized");
    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(s_spi_device, portMAX_DELAY), TAG,
                        "Failed to acquire SPI bus");

    const esp_err_t result = gpio_set_level(BSP_CH376S_CS_GPIO, 0);
    if (result != ESP_OK) {
        spi_device_release_bus(s_spi_device);
    }
    return result;
}

static void end_command(void)
{
    gpio_set_level(BSP_CH376S_CS_GPIO, 1);
    spi_device_release_bus(s_spi_device);
}

esp_err_t bsp_ch376_init(void)
{
    if (s_spi_device != NULL) {
        return ESP_OK;
    }

    const gpio_config_t cs_config = {
        .pin_bit_mask = (1ULL << BSP_SD_CS_GPIO) |
                        (1ULL << BSP_CH376S_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cs_config), TAG, "Failed to configure CS pins");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_SD_CS_GPIO, 1), TAG,
                        "Failed to deselect TF card");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_CH376S_CS_GPIO, 1), TAG,
                        "Failed to deselect CH376S");

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_SPI_MOSI_GPIO,
        .miso_io_num = BSP_SPI_MISO_GPIO,
        .sclk_io_num = BSP_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8,
    };
    esp_err_t result = spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_DISABLED);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI3: %s", esp_err_to_name(result));
        return result;
    }
    s_bus_initialized = true;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    result = spi_bus_add_device(SPI3_HOST, &device_config, &s_spi_device);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add CH376S SPI device: %s", esp_err_to_name(result));
        spi_bus_free(SPI3_HOST);
        s_bus_initialized = false;
        return result;
    }

    ESP_LOGI(TAG, "CH376S SPI initialized: SCLK=%d, MISO=%d, MOSI=%d, CS=%d",
             BSP_SPI_SCLK_GPIO, BSP_SPI_MISO_GPIO,
             BSP_SPI_MOSI_GPIO, BSP_CH376S_CS_GPIO);
    return ESP_OK;
}

void bsp_ch376_deinit(void)
{
    gpio_set_level(BSP_CH376S_CS_GPIO, 1);

    if (s_spi_device != NULL) {
        spi_bus_remove_device(s_spi_device);
        s_spi_device = NULL;
    }
    if (s_bus_initialized) {
        spi_bus_free(SPI3_HOST);
        s_bus_initialized = false;
    }
}

esp_err_t bsp_ch376_check_exist(uint8_t challenge, uint8_t *response)
{
    ESP_RETURN_ON_FALSE(response != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Response pointer is NULL");
    *response = 0;

    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start CHECK_EXIST");

    esp_err_t result = transfer_byte(CH376_CMD_CHECK_EXIST, NULL);
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(challenge, NULL);
    }
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(0xFF, response);
    }
    end_command();

    ESP_RETURN_ON_ERROR(result, TAG, "CHECK_EXIST SPI transfer failed");

    const uint8_t expected = (uint8_t)~challenge;
    if (*response != expected) {
        ESP_LOGE(TAG, "CH376S CHECK_EXIST mismatch: 0x%02X -> 0x%02X, expected 0x%02X",
                 challenge, *response, expected);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "CH376S SPI communication OK: 0x%02X -> 0x%02X",
             challenge, *response);
    return ESP_OK;
}

esp_err_t bsp_ch376_get_version(uint8_t *version)
{
    ESP_RETURN_ON_FALSE(version != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Version pointer is NULL");
    *version = 0;

    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start GET_IC_VER");

    esp_err_t result = transfer_byte(CH376_CMD_GET_IC_VER, NULL);
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(0xFF, version);
    }
    end_command();

    ESP_RETURN_ON_ERROR(result, TAG, "GET_IC_VER SPI transfer failed");
    ESP_LOGI(TAG, "CH376S version raw: 0x%02X", *version);
    return ESP_OK;
}
