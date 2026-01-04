/**
 * @file sensor_ir.c
 * @brief Obsługa czujnika IR Obstacle (KY-032)
 * 
 * Czujnik IR Obstacle może być podłączony do dowolnego pinu GPIO i nie wymaga pull-up.
 */

#include "sensor_ir.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SENSOR_IR";

#define MAX_IR_SENSORS 8  // Maksymalna liczba jednocześnie obsługiwanych czujników IR

static bool initialized_pins[MAX_IR_SENSORS] = {false};
static gpio_num_t initialized_gpio[MAX_IR_SENSORS] = {0};
static int num_initialized = 0;

static int find_pin_index(gpio_num_t pin)
{
    for (int i = 0; i < num_initialized; i++) {
        if (initialized_gpio[i] == pin) {
            return i;
        }
    }
    return -1;
}

esp_err_t sensor_ir_init(gpio_num_t pin)
{
    // Sprawdź czy pin już jest zainicjalizowany
    if (find_pin_index(pin) >= 0) {
        ESP_LOGW(TAG, "Pin GPIO %d już jest zainicjalizowany", pin);
        return ESP_OK;
    }
    
    // Sprawdź czy nie przekroczono limitu
    if (num_initialized >= MAX_IR_SENSORS) {
        ESP_LOGE(TAG, "Osiągnięto maksymalną liczbę czujników IR (%d)", MAX_IR_SENSORS);
        return ESP_ERR_NO_MEM;
    }
    
    // Konfiguracja IR Obstacle - bez pull-up
    gpio_config_t io_conf_ir = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf_ir);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji GPIO %d dla czujnika IR: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Zapisz zainicjalizowany pin
    int index = num_initialized++;
    initialized_pins[index] = true;
    initialized_gpio[index] = pin;
    
    ESP_LOGI(TAG, "Czujnik IR Obstacle zainicjalizowany (GPIO %d)", pin);
    return ESP_OK;
}

int sensor_ir_read(gpio_num_t pin)
{
    int index = find_pin_index(pin);
    if (index < 0 || !initialized_pins[index]) {
        ESP_LOGW(TAG, "Czujnik IR na pinie GPIO %d nie jest zainicjalizowany", pin);
        return -1;
    }
    
    return !gpio_get_level(pin);
}

bool sensor_ir_is_initialized(gpio_num_t pin)
{
    return find_pin_index(pin) >= 0;
}

