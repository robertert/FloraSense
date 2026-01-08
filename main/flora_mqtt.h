#ifndef FLORASENSE_MQTT_CLIENT_H
#define FLORASENSE_MQTT_CLIENT_H

#include "sensor_light.h"
#include "driver/gpio.h"

void mqtt_app_start(void);
void mqtt_publish_task(void *pvParameters);

// Funkcje getter dla konfiguracji czujników
sensor_light_config_t* mqtt_get_light_config_1(void);
sensor_light_config_t* mqtt_get_light_config_2(void);
gpio_num_t mqtt_get_ir_sensor_pin_1(void);  // Czujnik IR z tyłu
gpio_num_t mqtt_get_ir_sensor_pin_2(void);  // Czujnik IR z przodu

#endif

