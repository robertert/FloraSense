#pragma once

#include "esp_err.h"

typedef struct {
    float temperature_c;      // temperatura w stopniach Celsjusza
    float humidity_percent;   // wilgotność względna w %
    int32_t raw_temp;         // surowa wartość temperatury (do debugowania)
    int32_t raw_humidity;     // surowa wartość wilgotności (do debugowania)
} sensor_temp_reading_t;

esp_err_t sensor_temp_init(void);
esp_err_t sensor_temp_read(sensor_temp_reading_t *reading);

/**
 * @brief Task do obsługi czujnika temperatury BME280
 * 
 * Wykonuje inicjalizację czujnika, a następnie odczytuje dane
 * (temperatura i wilgotność) co określony interwał.
 * 
 * @param param Parametr taska (nieużywany)
 */
void sensor_temp_task(void *param);
