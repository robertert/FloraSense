#pragma once

#include "esp_err.h"

typedef struct {
    int raw_value;          // średnia wartość ADC
    int millivolts;         // napięcie po kalibracji
    float moisture_percent; // wynik przeskalowany 0-100%
} sensor_soil_reading_t;

esp_err_t sensor_soil_init(void);
esp_err_t sensor_soil_read(sensor_soil_reading_t *reading);