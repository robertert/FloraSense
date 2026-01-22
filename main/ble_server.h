#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*ble_server_water_cmd_cb_t)(uint32_t duration_ms);

void ble_server_init(void);
void ble_server_register_handlers(ble_server_water_cmd_cb_t water_cb);
void ble_server_set_hall_state(uint8_t hall_state);
bool ble_server_is_connected(void);

#ifdef __cplusplus
}
#endif