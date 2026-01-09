#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dock_control_hall_cb_t)(uint8_t hall_state);

esp_err_t dock_control_init(gpio_num_t hall_gpio, gpio_num_t pump_gpio, dock_control_hall_cb_t hall_cb);
uint8_t dock_control_get_hall_state(void);
bool dock_control_is_pump_active(void);
esp_err_t dock_control_handle_water_command(uint32_t duration_ms);
void dock_control_stop_pump(void);

#ifdef __cplusplus
}
#endif

