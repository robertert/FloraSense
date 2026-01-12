#pragma once

#include "esp_err.h"

typedef struct {
    int raw_value;          // średnia wartość ADC
    int millivolts;         // napięcie baterii w miliwoltach (po uwzględnieniu dzielnika)
    float battery_percent;  // poziom baterii w procentach (0-100%)
} sensor_battery_reading_t;

/**
 * @brief Inicjalizuje moduł monitorowania baterii
 * 
 * Konfiguruje ADC do odczytu napięcia baterii.
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t sensor_battery_init(void);

/**
 * @brief Odczytuje aktualny poziom baterii
 * 
 * @param reading Wskaźnik na strukturę do przechowania odczytu
 * @return ESP_OK jeśli odczyt się powiódł
 */
esp_err_t sensor_battery_read(sensor_battery_reading_t *reading);

/**
 * @brief Sprawdza czy moduł baterii jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool sensor_battery_is_initialized(void);

/**
 * @brief Aktualizuje stan LED ostrzegawczego na podstawie poziomu baterii
 * 
 * Funkcja powinna być wywoływana regularnie po odczycie baterii.
 * Miganie LED jest obsługiwane w tle przez task.
 * 
 * @param battery_percent Aktualny poziom baterii w procentach
 * @param threshold Próg niskiego poziomu baterii (opcjonalny, 0 = użyj domyślnego z config.h)
 */
void sensor_battery_update_led(float battery_percent, float threshold);

