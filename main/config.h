#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c.h"

#define EXAMPLE_ESP_WIFI_SSID      "iPhone Robert"
#define EXAMPLE_ESP_WIFI_PASS      "SWER1234"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10000
#define BLINK_GPIO 2

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Konfiguracja czujnika gleby (pojemnościowy analog)
#define SOIL_SENSOR_ADC_UNIT            ADC_UNIT_1
#define SOIL_SENSOR_ADC_CHANNEL         ADC_CHANNEL_6   // GPIO34
#define SOIL_SENSOR_ADC_ATTEN           ADC_ATTEN_DB_12
#define SOIL_SENSOR_ADC_BITWIDTH        ADC_BITWIDTH_12
#define SOIL_SENSOR_SAMPLE_COUNT        32
#define SOIL_SENSOR_POLL_PERIOD_MS      2000

// Kalibracja mapowania napięcia (mV)
#define SOIL_SENSOR_MV_AT_DRY           3000
#define SOIL_SENSOR_MV_AT_SATURATED     1100

// Konfiguracja czujnika światła (I2C, np. BH1750)
#define LIGHT_SENSOR_I2C_PORT           I2C_NUM_0
#define LIGHT_SENSOR_I2C_SDA_GPIO         GPIO_NUM_21
#define LIGHT_SENSOR_I2C_SCL_GPIO         GPIO_NUM_22
#define LIGHT_SENSOR_I2C_FREQ_HZ          50000
#define VEML7700_I2C_ADDRESS              0x10 // 0x48
#define VEML7700_REG_ALS_CONF             0x00
#define VEML7700_REG_ALS_WH               0x01
#define VEML7700_REG_ALS_WL               0x02
#define VEML7700_REG_POWER_SAVING         0x03
#define VEML7700_REG_ALS                  0x04
#define VEML7700_REG_WHITE                0x05
#define VEML7700_REG_ALS_INT              0x06

#define VEML7700_GAIN_1X                  0x00
#define VEML7700_GAIN_2X                  0x01
#define VEML7700_GAIN_1_DIV_8             0x02
#define VEML7700_GAIN_1_DIV_4             0x03

#define VEML7700_IT_25MS                  0x0C
#define VEML7700_IT_50MS                  0x08
#define VEML7700_IT_100MS                 0x00
#define VEML7700_IT_200MS                 0x01
#define VEML7700_IT_400MS                 0x02
#define VEML7700_IT_800MS                 0x03

#define VEML7700_PERSISTENCE_1            0x00
#define VEML7700_INT_DISABLE              0x00
#define VEML7700_INT_ENABLE               0x01
#define VEML7700_POWER_ON                 0x00
#define VEML7700_POWER_OFF                0x01

#define VEML7700_DEFAULT_GAIN             VEML7700_GAIN_1X
#define VEML7700_DEFAULT_IT               VEML7700_IT_100MS
#define LIGHT_SENSOR_POLL_PERIOD_MS       3000