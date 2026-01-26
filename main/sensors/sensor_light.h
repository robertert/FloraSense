#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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

/**
 * @brief Pobierz lub utwórz mutex I2C dla danego portu
 *
 * Używaj tej funkcji aby uzyskać wspólny mutex dla wszystkich urządzeń na danym porcie I2C.
 * @param port Port I2C
 * @return SemaphoreHandle_t Mutex lub NULL jeśli błąd
 */
SemaphoreHandle_t sensor_light_get_i2c_mutex(i2c_port_t port);
