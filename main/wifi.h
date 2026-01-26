#pragma once

#include "esp_err.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

#include "config.h"

typedef struct {
    char ssid[32];
    char password[64];
    char custom_param[32];
} app_config_t;

extern app_config_t current_config;

void wifi_manager_init(void);
bool wifi_is_connected(void);
bool wifi_is_ap_mode(void);
void clear_nvs_config(void);