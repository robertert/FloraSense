#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

typedef struct {
    float lux;
    uint16_t raw_als;
    uint16_t raw_white;
} sensor_light_reading_t;

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t i2c_freq_hz;
    uint8_t i2c_address;
} sensor_light_config_t;

esp_err_t sensor_light_init(const sensor_light_config_t *config);
esp_err_t sensor_light_read(const sensor_light_config_t *config, sensor_light_reading_t *reading);
bool sensor_light_is_initialized(const sensor_light_config_t *config);
void light_sensor_task(void *param);
