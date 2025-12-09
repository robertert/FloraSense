#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// Stałe I2C dla BMP280
#define BMP280_I2C_ADDRESS    0x76
#define BMP280_TEMP_REG_SIZE  3

// Funkcje publiczne
esp_err_t bmp280_init(void);
double bmp280_read_temperature(void);
bool bmp280_is_initialized(void);