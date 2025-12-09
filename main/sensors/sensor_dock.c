/**
 * @file sensor_dock.c
 * @brief Obsługa czujnika dock (microswitch)
 * 
 * Microswitch jest podłączony do GPIO 19 i wymaga wbudowanego pull-up.
 */

#include "sensor_dock.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "SENSOR_DOCK";

#define DOCK_SWITCH_GPIO        19  // Wymaga pull-up

static bool initialized = false;

esp_err_t sensor_dock_init(void)
{
    // Konfiguracja Microswitch (GPIO 19) - z pull-up
    gpio_config_t io_conf_switch = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << DOCK_SWITCH_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf_switch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji GPIO dla czujnika dock: %s", esp_err_to_name(ret));
        initialized = false;
        return ret;
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Czujnik dock zainicjalizowany (GPIO %d)", DOCK_SWITCH_GPIO);
    return ESP_OK;
}

int sensor_dock_read(void)
{
    if (!initialized) {
        ESP_LOGW(TAG, "Czujnik dock nie jest zainicjalizowany");
        return -1;
    }
    
    return gpio_get_level(DOCK_SWITCH_GPIO);
}

bool sensor_dock_is_initialized(void)
{
    return initialized;
}

