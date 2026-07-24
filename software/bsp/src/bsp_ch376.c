#include "bsp_ch376.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_ch376";

enum {
    CH376_CMD_GET_IC_VER = 0x01,
    CH376_CMD_RESET_ALL = 0x05,
    CH376_CMD_CHECK_EXIST = 0x06,
    CH376_CMD_SET_USB_MODE = 0x15,
    CH376_CMD_GET_STATUS = 0x22,
    CH376_CMD_RD_USB_DATA0 = 0x27,
    CH376_CMD_SET_FILE_NAME = 0x2F,
    CH376_CMD_DISK_CONNECT = 0x30,
    CH376_CMD_DISK_MOUNT = 0x31,
    CH376_CMD_FILE_OPEN = 0x32,
    CH376_CMD_FILE_CLOSE = 0x36,
    CH376_CMD_BYTE_READ = 0x3A,
    CH376_CMD_BYTE_RD_GO = 0x3B,
    CH376_USB_MODE_HOST = 0x06,
    CH376_CLOSE_UPDATE_LENGTH = 0x00,
    CH376_RET_SUCCESS = 0x51,
    CH376_STATUS_USB_INT_SUCCESS = 0x14,
    CH376_STATUS_USB_INT_CONNECT = 0x15,
    CH376_STATUS_USB_INT_USB_READY = 0x18,
    CH376_STATUS_USB_INT_DISK_READ = 0x1D,
    CH376_STATUS_GET_STATUS_ECHO = 0x22,
    CH376_STATUS_ERR_OPEN_DIR = 0x41,
    CH376_MOUNT_ATTEMPTS = 5,
    CH376_RESET_DELAY_MS = 80,
    CH376_SINGLE_PACKET_READ = 64,
    CH376_MAX_FILE_READ_CHUNK = 255,
    CH376_COMMAND_TIMEOUT_MS = 2000,
    CH376_POLL_DELAY_MS = 10,
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

static esp_err_t command_write_bytes(const uint8_t *bytes, size_t length)
{
    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start CH376 command");

    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < length && result == ESP_OK; ++i) {
        result = transfer_byte(bytes[i], NULL);
        esp_rom_delay_us(CH376_TSC_DELAY_US);
    }

    end_command();
    return result;
}

static esp_err_t read_status(uint8_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Status pointer is NULL");

    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start GET_STATUS");
    esp_err_t result = transfer_byte(CH376_CMD_GET_STATUS, NULL);
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(0xFF, status);
    }
    end_command();
    return result;
}

static esp_err_t wait_status(uint8_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Status pointer is NULL");

    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(CH376_COMMAND_TIMEOUT_MS);

    do {
        ESP_RETURN_ON_ERROR(read_status(status), TAG, "Failed to read CH376 status");
        if (*status != 0x00 && *status != 0xFF &&
            *status != CH376_STATUS_GET_STATUS_ECHO) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(CH376_POLL_DELAY_MS));
    } while (xTaskGetTickCount() < deadline);

    ESP_LOGE(TAG, "Timed out waiting for CH376 status");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t reset_chip(void)
{
    const uint8_t reset_all = CH376_CMD_RESET_ALL;
    ESP_RETURN_ON_ERROR(command_write_bytes(&reset_all, 1), TAG,
                        "RESET_ALL transfer failed");
    vTaskDelay(pdMS_TO_TICKS(CH376_RESET_DELAY_MS));
    ESP_LOGI(TAG, "CH376S reset finished");
    return ESP_OK;
}

static bool status_is_usb_ready(uint8_t status)
{
    return status == CH376_STATUS_USB_INT_SUCCESS ||
           status == CH376_STATUS_USB_INT_CONNECT ||
           status == CH376_STATUS_USB_INT_USB_READY;
}

static esp_err_t connect_usb_disk(void)
{
    const uint8_t disk_connect = CH376_CMD_DISK_CONNECT;
    ESP_RETURN_ON_ERROR(command_write_bytes(&disk_connect, 1), TAG,
                        "DISK_CONNECT transfer failed");

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(wait_status(&status), TAG, "DISK_CONNECT wait failed");
    ESP_LOGI(TAG, "DISK_CONNECT status=0x%02X", status);
    if (!status_is_usb_ready(status)) {
        ESP_LOGE(TAG, "DISK_CONNECT failed, CH376 status=0x%02X", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t mount_usb_disk(void)
{
    const uint8_t disk_mount = CH376_CMD_DISK_MOUNT;
    uint8_t last_status = 0;

    for (int attempt = 1; attempt <= CH376_MOUNT_ATTEMPTS; ++attempt) {
        ESP_RETURN_ON_ERROR(command_write_bytes(&disk_mount, 1), TAG,
                            "DISK_MOUNT transfer failed");

        uint8_t status = 0;
        ESP_RETURN_ON_ERROR(wait_status(&status), TAG, "DISK_MOUNT wait failed");
        last_status = status;
        ESP_LOGI(TAG, "DISK_MOUNT attempt %d/%d status=0x%02X", attempt,
                 CH376_MOUNT_ATTEMPTS, status);
        if (status == CH376_STATUS_USB_INT_SUCCESS) {
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGE(TAG, "DISK_MOUNT failed, last CH376 status=0x%02X", last_status);
    return ESP_FAIL;
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
    bsp_ch376_file_close();
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

esp_err_t bsp_ch376_usb_disk_mount(void)
{
    ESP_RETURN_ON_ERROR(bsp_ch376_init(), TAG, "CH376 init failed");
    ESP_RETURN_ON_ERROR(reset_chip(), TAG, "CH376 reset failed");

    const uint8_t set_usb_mode[] = {
        CH376_CMD_SET_USB_MODE,
        CH376_USB_MODE_HOST,
    };

    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start SET_USB_MODE");
    esp_err_t result = transfer_byte(set_usb_mode[0], NULL);
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(set_usb_mode[1], NULL);
    }
    uint8_t response = 0;
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
        result = transfer_byte(0xFF, &response);
    }
    end_command();

    ESP_RETURN_ON_ERROR(result, TAG, "SET_USB_MODE transfer failed");
    if (response != CH376_RET_SUCCESS) {
        ESP_LOGE(TAG, "SET_USB_MODE failed: response=0x%02X", response);
        return ESP_ERR_INVALID_RESPONSE;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(connect_usb_disk(), TAG, "DISK_CONNECT status failed");
    ESP_RETURN_ON_ERROR(mount_usb_disk(), TAG, "DISK_MOUNT status failed");

    ESP_LOGI(TAG, "USB disk mounted through CH376S");
    return ESP_OK;
}

esp_err_t bsp_ch376_file_open(const char *path)
{
    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "File path is NULL");
    ESP_RETURN_ON_ERROR(bsp_ch376_init(), TAG, "CH376 init failed");

    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start SET_FILE_NAME");
    esp_err_t result = transfer_byte(CH376_CMD_SET_FILE_NAME, NULL);
    for (size_t i = 0; result == ESP_OK && path[i] != '\0'; ++i) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte((uint8_t)path[i], NULL);
    }
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(0x00, NULL);
    }
    end_command();
    ESP_RETURN_ON_ERROR(result, TAG, "SET_FILE_NAME transfer failed");

    const uint8_t file_open = CH376_CMD_FILE_OPEN;
    ESP_RETURN_ON_ERROR(command_write_bytes(&file_open, 1), TAG,
                        "FILE_OPEN transfer failed");

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(wait_status(&status), TAG, "FILE_OPEN wait failed");
    if (status != CH376_STATUS_USB_INT_SUCCESS) {
        ESP_LOGE(TAG, "FILE_OPEN failed for %s, CH376 status=0x%02X", path,
                 status);
        return status == CH376_STATUS_ERR_OPEN_DIR ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    ESP_LOGI(TAG, "USB file opened: %s", path);
    return ESP_OK;
}

static esp_err_t read_usb_data0(uint8_t *buffer, size_t buffer_size,
                                size_t *bytes_read)
{
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Read buffer is NULL");
    ESP_RETURN_ON_FALSE(bytes_read != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bytes_read is NULL");
    *bytes_read = 0;

    ESP_RETURN_ON_ERROR(begin_command(), TAG, "Failed to start RD_USB_DATA0");
    esp_err_t result = transfer_byte(CH376_CMD_RD_USB_DATA0, NULL);

    uint8_t available = 0;
    if (result == ESP_OK) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(0xFF, &available);
    }

    const size_t to_read = available > buffer_size ? buffer_size : available;
    for (size_t i = 0; result == ESP_OK && i < to_read; ++i) {
        esp_rom_delay_us(CH376_TSC_DELAY_US);
        result = transfer_byte(0xFF, &buffer[i]);
    }
    end_command();

    ESP_RETURN_ON_ERROR(result, TAG, "RD_USB_DATA0 transfer failed");
    *bytes_read = to_read;
    return ESP_OK;
}

esp_err_t bsp_ch376_file_read(uint8_t *buffer, size_t buffer_size,
                              size_t *bytes_read)
{
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Read buffer is NULL");
    ESP_RETURN_ON_FALSE(bytes_read != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bytes_read is NULL");
    *bytes_read = 0;
    if (buffer_size == 0) {
        return ESP_OK;
    }

    const uint16_t request =
        (uint16_t)(buffer_size > CH376_SINGLE_PACKET_READ
                       ? CH376_SINGLE_PACKET_READ
                       : buffer_size);
    const uint8_t byte_read[] = {
        CH376_CMD_BYTE_READ,
        (uint8_t)(request & 0xFFU),
        (uint8_t)(request >> 8U),
    };
    ESP_RETURN_ON_ERROR(command_write_bytes(byte_read, sizeof(byte_read)), TAG,
                        "BYTE_READ transfer failed");

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(wait_status(&status), TAG, "BYTE_READ wait failed");
    if (status == CH376_STATUS_USB_INT_SUCCESS) {
        return ESP_OK;
    }
    if (status != CH376_STATUS_USB_INT_DISK_READ) {
        ESP_LOGE(TAG, "BYTE_READ failed, CH376 status=0x%02X", status);
        return ESP_FAIL;
    }

    return read_usb_data0(buffer, request, bytes_read);
}

void bsp_ch376_file_close(void)
{
    if (s_spi_device == NULL) {
        return;
    }

    const uint8_t file_close[] = {
        CH376_CMD_FILE_CLOSE,
        CH376_CLOSE_UPDATE_LENGTH,
    };
    (void)command_write_bytes(file_close, sizeof(file_close));
}
