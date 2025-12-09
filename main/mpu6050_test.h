#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Funkcje publiczne
/**
 * @brief Uruchamia task demonstracyjny MPU6050.
 *
 * Task:
 *  - inicjalizuje sensor (I2C_NUM_0, piny 21/22, adres 0x68),
 *  - wykonuje serię przykładowych odczytów i zmian konfiguracji,
 *  - testuje FIFO i flagę DATA_READY,
 *  - usypia czujnik i zwalnia zasoby.
 */
void mpu6050_test_start(void);
