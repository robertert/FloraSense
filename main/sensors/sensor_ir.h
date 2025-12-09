#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Inicjalizuje czujnik IR Obstacle (KY-032)
 * 
 * Konfiguruje GPIO 23 jako wejście bez pull-up.
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t sensor_ir_init(void);

/**
 * @brief Odczytuje stan czujnika IR Obstacle
 * 
 * @return 1 jeśli wykryto przeszkodę, 0 w przeciwnym razie
 */
int sensor_ir_read(void);

/**
 * @brief Sprawdza czy czujnik IR Obstacle jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool sensor_ir_is_initialized(void);

