#pragma once

#include "driver/gpio.h"

/* Shared I2C bus: OLED and ES8388 control interface. */
#define BSP_I2C_SDA_GPIO          GPIO_NUM_21
#define BSP_I2C_SCL_GPIO          GPIO_NUM_22

/* I2S0 output: two MAX98357A amplifiers share these signals. */
#define BSP_AMP_BCLK_GPIO         GPIO_NUM_26
#define BSP_AMP_LRCLK_GPIO        GPIO_NUM_25
#define BSP_AMP_DOUT_GPIO         GPIO_NUM_27
#define BSP_AMP_SD_GPIO           GPIO_NUM_16

/* I2S1 input: ES8388 AUX audio capture. */
#define BSP_AUX_MCLK_GPIO         GPIO_NUM_0
#define BSP_AUX_BCLK_GPIO         GPIO_NUM_32
#define BSP_AUX_LRCLK_GPIO        GPIO_NUM_17
#define BSP_AUX_DIN_GPIO          GPIO_NUM_35

/* Shared VSPI bus: TF/SD card and CH376S use separate chip selects. */
#define BSP_SPI_SCLK_GPIO         GPIO_NUM_18
#define BSP_SPI_MISO_GPIO         GPIO_NUM_19
#define BSP_SPI_MOSI_GPIO         GPIO_NUM_23
#define BSP_SD_CS_GPIO            GPIO_NUM_5
#define BSP_CH376S_CS_GPIO        GPIO_NUM_15
#define BSP_CH376S_RST_GPIO       GPIO_NUM_4

/* User input. GPIO34 and GPIO39 require external pull-up resistors. */
#define BSP_ENCODER_A_GPIO        GPIO_NUM_34
#define BSP_ENCODER_B_GPIO        GPIO_NUM_39
#define BSP_BUTTON_PLAY_GPIO      GPIO_NUM_13
#define BSP_BUTTON_SOURCE_GPIO    GPIO_NUM_14

/* Battery voltage divider ADC input. */
#define BSP_BAT_ADC_GPIO          GPIO_NUM_36
