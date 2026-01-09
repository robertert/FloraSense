/**
 * @file sensor_hall.c
 * @brief Obsługa czujnika Hall (A3144)
 * 
 * Czujnik Hall jest podłączony do GPIO 18 i wymaga wbudowanego pull-up.
 */

#include "sensor_hall.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "config.h"

static const char *TAG = "SENSOR_HALL";

static bool initialized = false;

esp_err_t sensor_hall_init(void)
{
    // Konfiguracja Hall Sensor (GPIO DOCK_HALL_GPIO) - z pull-up
    gpio_config_t io_conf_hall = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << DOCK_HALL_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf_hall);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji GPIO dla czujnika Hall: %s", esp_err_to_name(ret));
        initialized = false;
        return ret;
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Czujnik Hall zainicjalizowany (GPIO %d)", DOCK_HALL_GPIO);
    return ESP_OK;
}

int sensor_hall_read(void)
{
    if (!initialized) {
        ESP_LOGW(TAG, "Czujnik Hall nie jest zainicjalizowany");
        return -1;
    }
    
    return gpio_get_level(DOCK_HALL_GPIO);
}

bool sensor_hall_is_initialized(void)
{
    return initialized;
}

