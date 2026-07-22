#include "bsp_oled.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_pins.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_oled";

enum {
    OLED_I2C_PORT = I2C_NUM_0,
    OLED_I2C_ADDRESS = 0x3C,
    OLED_I2C_FREQ_HZ = 400000,
    OLED_CONTROL_COMMAND = 0x00,
    OLED_CONTROL_DATA = 0x40,
    OLED_WIDTH = 128,
    OLED_PAGES = 8,
};

static bool s_oled_initialized;

static esp_err_t oled_write(uint8_t control, const uint8_t *data, size_t length)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "OLED data is NULL");
    uint8_t buffer[1 + OLED_WIDTH] = {control};

    ESP_RETURN_ON_FALSE(length <= OLED_WIDTH, ESP_ERR_INVALID_SIZE, TAG,
                        "OLED transfer too large");

    for (size_t i = 0; i < length; ++i) {
        buffer[1 + i] = data[i];
    }

    return i2c_master_write_to_device(OLED_I2C_PORT, OLED_I2C_ADDRESS,
                                      buffer, length + 1,
                                      pdMS_TO_TICKS(100));
}

static esp_err_t oled_write_command(uint8_t command)
{
    return oled_write(OLED_CONTROL_COMMAND, &command, 1);
}

static esp_err_t oled_set_page(uint8_t page)
{
    ESP_RETURN_ON_ERROR(oled_write_command((uint8_t)(0xB0 | page)), TAG,
                        "Failed to set OLED page");
    ESP_RETURN_ON_ERROR(oled_write_command(0x00), TAG,
                        "Failed to set OLED lower column");
    return oled_write_command(0x10);
}

esp_err_t bsp_oled_init(void)
{
    if (!s_oled_initialized) {
        const i2c_config_t i2c_config = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = BSP_I2C_SDA_GPIO,
            .scl_io_num = BSP_I2C_SCL_GPIO,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = OLED_I2C_FREQ_HZ,
            .clk_flags = 0,
        };

        ESP_RETURN_ON_ERROR(i2c_param_config(OLED_I2C_PORT, &i2c_config), TAG,
                            "Failed to configure OLED I2C");

        esp_err_t result = i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install OLED I2C driver: %s",
                     esp_err_to_name(result));
            return result;
        }

        s_oled_initialized = true;
    }

    static const uint8_t init_commands[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* display clock */
        0xA8, 0x3F, /* multiplex 1/64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* display start line */
        0x8D, 0x14, /* charge pump on */
        0x20, 0x00, /* horizontal addressing mode */
        0xA1,       /* segment remap */
        0xC8,       /* COM scan direction */
        0xDA, 0x12, /* COM pins */
        0x81, 0xCF, /* contrast */
        0xD9, 0xF1, /* pre-charge */
        0xDB, 0x40, /* VCOMH */
        0xA4,       /* resume RAM display */
        0xA6,       /* normal display */
        0xAF,       /* display on */
    };

    for (size_t i = 0; i < sizeof(init_commands); ++i) {
        ESP_RETURN_ON_ERROR(oled_write_command(init_commands[i]), TAG,
                            "Failed to send OLED init command");
    }

    ESP_LOGI(TAG, "OLED initialized: address=0x%02X, SDA=%d, SCL=%d",
             OLED_I2C_ADDRESS, BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO);
    return ESP_OK;
}

esp_err_t bsp_oled_run_self_test(void)
{
    ESP_RETURN_ON_ERROR(bsp_oled_init(), TAG, "OLED init failed");

    uint8_t line[OLED_WIDTH];
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        for (size_t x = 0; x < OLED_WIDTH; ++x) {
            line[x] = ((x / 8U + page) % 2U) == 0U ? 0xFF : 0x00;
        }

        ESP_RETURN_ON_ERROR(oled_set_page(page), TAG, "Failed to select OLED page");
        ESP_RETURN_ON_ERROR(oled_write(OLED_CONTROL_DATA, line, sizeof(line)), TAG,
                            "Failed to write OLED self-test pattern");
    }

    ESP_LOGI(TAG, "OLED self-test pattern displayed");
    return ESP_OK;
}
