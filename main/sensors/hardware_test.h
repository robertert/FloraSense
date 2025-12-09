#pragma once

#include "esp_err.h"

/**
 * @brief Inicjalizuje i uruchamia aplikację testową sprzętu
 * 
 * Ta funkcja konfiguruje:
 * - GPIO dla silników DC (TB6612FNG)
 * - GPIO dla pompy wody
 * - GPIO dla czujników (Hall, Microswitch, IR)
 * - LEDC dla kontroli PWM silników
 * - Zadania FreeRTOS dla kontroli UART i monitorowania czujników
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t hardware_test_init(void);

