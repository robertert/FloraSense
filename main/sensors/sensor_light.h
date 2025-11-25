#pragma once

#include "esp_err.h"

typedef struct {
    float lux;
    uint16_t raw_als;
    uint16_t raw_white;
} sensor_light_reading_t;

esp_err_t sensor_light_init(void);
esp_err_t sensor_light_read(sensor_light_reading_t *reading);
void light_sensor_task(void *param);
