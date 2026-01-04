#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

/**
 * @brief Inicjalizuje czujnik IR Obstacle (KY-032)
 * 
 * Konfiguruje podany pin GPIO jako wejście bez pull-up.
 * 
 * @param pin Numer pinu GPIO do użycia
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t sensor_ir_init(gpio_num_t pin);

/**
 * @brief Odczytuje stan czujnika IR Obstacle
 * 
 * @param pin Numer pinu GPIO do odczytu
 * @return 1 jeśli wykryto przeszkodę, 0 w przeciwnym razie, -1 w przypadku błędu
 */
int sensor_ir_read(gpio_num_t pin);

/**
 * @brief Sprawdza czy czujnik IR Obstacle jest zainicjalizowany
 * 
 * @param pin Numer pinu GPIO do sprawdzenia
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool sensor_ir_is_initialized(gpio_num_t pin);

