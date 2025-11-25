#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    uint16_t soil_moisture_pct;
    uint16_t light_lux;
} ble_server_measurements_t;

typedef void (*ble_server_pump_command_cb_t)(bool pump_on);

void ble_server_init(void);
void ble_server_update_measurements(const ble_server_measurements_t *measurements);
void ble_server_register_pump_callback(ble_server_pump_command_cb_t callback);

#ifdef __cplusplus
}
#endif