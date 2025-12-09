#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Inicjalizuje czujnik dock (microswitch)
 * 
 * Konfiguruje GPIO 19 jako wejście z wbudowanym pull-up.
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t sensor_dock_init(void);

/**
 * @brief Odczytuje stan czujnika dock
 * 
 * @return 1 jeśli switch jest wciśnięty, 0 w przeciwnym razie
 */
int sensor_dock_read(void);

/**
 * @brief Sprawdza czy czujnik dock jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool sensor_dock_is_initialized(void);

