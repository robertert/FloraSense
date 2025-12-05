#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"

// Stałe I2C dla BMP280
#define BMP280_I2C_ADDRESS    0x76
#define BMP280_TEMP_REG_SIZE  3

// Funkcje publiczne
void bmp280_init(void);
double bmp280_read_temperature(void);