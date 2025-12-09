/**
 * @file sensor_ir.c
 * @brief Obsługa czujnika IR Obstacle (KY-032)
 * 
 * Czujnik IR Obstacle jest podłączony do GPIO 23 i nie wymaga pull-up.
 */

#include "sensor_ir.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "SENSOR_IR";

#define IR_OBSTACLE_GPIO        23  // Bez pull-up

static bool initialized = false;

esp_err_t sensor_ir_init(void)
{
    // Konfiguracja IR Obstacle (GPIO 23) - bez pull-up
    gpio_config_t io_conf_ir = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << IR_OBSTACLE_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf_ir);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji GPIO dla czujnika IR: %s", esp_err_to_name(ret));
        initialized = false;
        return ret;
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Czujnik IR Obstacle zainicjalizowany (GPIO %d)", IR_OBSTACLE_GPIO);
    return ESP_OK;
}

int sensor_ir_read(void)
{
    if (!initialized) {
        ESP_LOGW(TAG, "Czujnik IR nie jest zainicjalizowany");
        return -1;
    }
    
    return gpio_get_level(IR_OBSTACLE_GPIO);
}

bool sensor_ir_is_initialized(void)
{
    return initialized;
}

