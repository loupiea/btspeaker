#include "bsp_oled.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
    OLED_FONT_WIDTH = 5,
    OLED_FONT_SPACING = 1,
};

static bool s_oled_initialized;

static const uint8_t s_font5x7[][OLED_FONT_WIDTH] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x08, 0x14, 0x22, 0x41, 0x00}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x00, 0x41, 0x22, 0x14, 0x08}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
};

static const uint8_t *font5x7_for_char(char character)
{
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    if (character < ' ' || character > 'Z') {
        character = ' ';
    }
    return s_font5x7[(uint8_t)(character - ' ')];
}

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
    esp_err_t result = oled_write(OLED_CONTROL_COMMAND, &command, 1);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "OLED command 0x%02X failed: %s",
                 command, esp_err_to_name(result));
    }
    return result;
}

static esp_err_t oled_set_page(uint8_t page)
{
    ESP_RETURN_ON_ERROR(oled_write_command((uint8_t)(0xB0 | page)), TAG,
                        "Failed to set OLED page");
    ESP_RETURN_ON_ERROR(oled_write_command(0x00), TAG,
                        "Failed to set OLED lower column");
    return oled_write_command(0x10);
}

static esp_err_t oled_draw_text_line(uint8_t page, const char *text)
{
    uint8_t line[OLED_WIDTH] = {0};
    size_t x = 0;

    if (text != NULL) {
        for (size_t i = 0; text[i] != '\0' && x < OLED_WIDTH; ++i) {
            const uint8_t *glyph = font5x7_for_char(text[i]);
            for (size_t col = 0; col < OLED_FONT_WIDTH && x < OLED_WIDTH; ++col) {
                line[x++] = glyph[col];
            }
            for (size_t col = 0; col < OLED_FONT_SPACING && x < OLED_WIDTH; ++col) {
                line[x++] = 0x00;
            }
        }
    }

    ESP_RETURN_ON_ERROR(oled_set_page(page), TAG, "Failed to select OLED text page");
    return oled_write(OLED_CONTROL_DATA, line, sizeof(line));
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

esp_err_t bsp_oled_clear(void)
{
    ESP_RETURN_ON_ERROR(bsp_oled_init(), TAG, "OLED init failed");

    uint8_t line[OLED_WIDTH] = {0};
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        ESP_RETURN_ON_ERROR(oled_set_page(page), TAG, "Failed to select OLED page");
        ESP_RETURN_ON_ERROR(oled_write(OLED_CONTROL_DATA, line, sizeof(line)), TAG,
                            "Failed to clear OLED page");
    }

    return ESP_OK;
}

esp_err_t bsp_oled_show_lines(const char *line1, const char *line2)
{
    ESP_RETURN_ON_ERROR(bsp_oled_clear(), TAG, "OLED clear failed");
    ESP_RETURN_ON_ERROR(oled_draw_text_line(0, line1), TAG,
                        "Failed to draw OLED line 1");
    ESP_RETURN_ON_ERROR(oled_draw_text_line(2, line2), TAG,
                        "Failed to draw OLED line 2");
    return ESP_OK;
}
