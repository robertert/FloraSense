#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Inicjalizuje czujnik Hall (A3144)
 * 
 * Konfiguruje GPIO 18 jako wejście z wbudowanym pull-up.
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t sensor_hall_init(void);

/**
 * @brief Odczytuje stan czujnika Hall
 * 
 * @return 1 jeśli wykryto pole magnetyczne, 0 w przeciwnym razie
 */
int sensor_hall_read(void);

/**
 * @brief Sprawdza czy czujnik Hall jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool sensor_hall_is_initialized(void);

