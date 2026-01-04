/**
 * @file motor_controller.c
 * @brief Sterownik silników DC (TB6612FNG lub podobny)
 * 
 * Steruje dwoma silnikami DC za pomocą PWM (prędkość) i pinów GPIO (kierunek).
 * Silnik A: PWMA=GPIO13, AIN1=GPIO27, AIN2=GPIO14
 * Silnik B: PWMB=GPIO4, BIN1=GPIO16, BIN2=GPIO17
 */

#include "motor_controller.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "MOTOR_CTRL";

static bool initialized = false;

// Konfiguracja PWM dla silników
#define MOTOR_A_PWM_CHANNEL    LEDC_CHANNEL_0
#define MOTOR_B_PWM_CHANNEL    LEDC_CHANNEL_1
#define MOTOR_PWM_TIMER        LEDC_TIMER_0

/**
 * @brief Inicjalizuje sterownik silników
 * 
 * Konfiguruje piny GPIO dla kierunku i PWM dla prędkości.
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t motor_controller_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Sterownik silników już zainicjalizowany");
        return ESP_OK;
    }

    // Opóźnienie inicjalizacji, aby uniknąć konfliktów podczas bootowania/flashowania
    // GPIO 4 jest strapping pinem i może interferować z flashowaniem
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t ret;

    // Konfiguracja pinów kierunku jako wyjścia GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << MOTOR_A_IN1_GPIO) | 
                       (1ULL << MOTOR_A_IN2_GPIO) |
                       (1ULL << MOTOR_B_IN1_GPIO) |
                       (1ULL << MOTOR_B_IN2_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji GPIO dla pinów kierunku: %s", esp_err_to_name(ret));
        return ret;
    }

    // Ustawienie pinów kierunku na LOW (stop)
    gpio_set_level(MOTOR_A_IN1_GPIO, 0);
    gpio_set_level(MOTOR_A_IN2_GPIO, 0);
    gpio_set_level(MOTOR_B_IN1_GPIO, 0);
    gpio_set_level(MOTOR_B_IN2_GPIO, 0);

    // Konfiguracja timer PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = MOTOR_PWM_TIMER,
        .duty_resolution  = MOTOR_PWM_RESOLUTION,
        .freq_hz          = MOTOR_PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji timer PWM: %s", esp_err_to_name(ret));
        return ret;
    }

    // Konfiguracja kanału PWM dla silnika A
    ledc_channel_config_t ledc_channel_a = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = MOTOR_A_PWM_CHANNEL,
        .timer_sel      = MOTOR_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_A_PWM_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ret = ledc_channel_config(&ledc_channel_a);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji kanału PWM dla silnika A: %s", esp_err_to_name(ret));
        return ret;
    }

    // Konfiguracja kanału PWM dla silnika B
    ledc_channel_config_t ledc_channel_b = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = MOTOR_B_PWM_CHANNEL,
        .timer_sel      = MOTOR_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_B_PWM_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ret = ledc_channel_config(&ledc_channel_b);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji kanału PWM dla silnika B: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Sterownik silników zainicjalizowany");
    ESP_LOGI(TAG, "  Silnik A: PWM=GPIO%d, IN1=GPIO%d, IN2=GPIO%d", 
             MOTOR_A_PWM_GPIO, MOTOR_A_IN1_GPIO, MOTOR_A_IN2_GPIO);
    ESP_LOGI(TAG, "  Silnik B: PWM=GPIO%d, IN1=GPIO%d, IN2=GPIO%d", 
             MOTOR_B_PWM_GPIO, MOTOR_B_IN1_GPIO, MOTOR_B_IN2_GPIO);
    
    return ESP_OK;
}

/**
 * @brief Ustawia prędkość i kierunek silnika A
 * 
 * @param speed Prędkość w zakresie -255 do 255 (ujemne = wstecz, dodatnie = przód, 0 = stop)
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_a_set_speed(int16_t speed)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Sterownik silników nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    // Ograniczenie zakresu prędkości
    if (speed > 255) speed = 255;
    if (speed < -255) speed = -255;

    uint32_t duty = (speed < 0) ? -speed : speed; // Wartość bezwzględna

    if (speed > 0) {
        // Przód: IN1=HIGH, IN2=LOW
        gpio_set_level(MOTOR_A_IN1_GPIO, 1);
        gpio_set_level(MOTOR_A_IN2_GPIO, 0);
        ESP_LOGD(TAG, "Silnik A: Przód - IN1=HIGH, IN2=LOW, PWM=%lu", duty);
    } else if (speed < 0) {
        // Wstecz: IN1=LOW, IN2=HIGH
        gpio_set_level(MOTOR_A_IN1_GPIO, 0);
        gpio_set_level(MOTOR_A_IN2_GPIO, 1);
        ESP_LOGD(TAG, "Silnik A: Wstecz - IN1=LOW, IN2=HIGH, PWM=%lu", duty);
    } else {
        // Stop: IN1=LOW, IN2=LOW
        gpio_set_level(MOTOR_A_IN1_GPIO, 0);
        gpio_set_level(MOTOR_A_IN2_GPIO, 0);
        ESP_LOGD(TAG, "Silnik A: Stop - IN1=LOW, IN2=LOW, PWM=0");
    }

    // Krótkie opóźnienie po ustawieniu pinów kierunku
    vTaskDelay(pdMS_TO_TICKS(1));

    // Ustawienie PWM
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_A_PWM_CHANNEL, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd ustawienia PWM dla silnika A: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_A_PWM_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd aktualizacji PWM dla silnika A: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief Ustawia prędkość i kierunek silnika B
 * 
 * @param speed Prędkość w zakresie -255 do 255 (ujemne = wstecz, dodatnie = przód, 0 = stop)
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_b_set_speed(int16_t speed)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Sterownik silników nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    // Ograniczenie zakresu prędkości
    if (speed > 255) speed = 255;
    if (speed < -255) speed = -255;

    uint32_t duty = (speed < 0) ? -speed : speed; // Wartość bezwzględna

    if (speed > 0) {
        // Przód: IN1=HIGH, IN2=LOW
        gpio_set_level(MOTOR_B_IN1_GPIO, 1);
        gpio_set_level(MOTOR_B_IN2_GPIO, 0);
    } else if (speed < 0) {
        // Wstecz: IN1=LOW, IN2=HIGH
        gpio_set_level(MOTOR_B_IN1_GPIO, 0);
        gpio_set_level(MOTOR_B_IN2_GPIO, 1);
    } else {
        // Stop: IN1=LOW, IN2=LOW
        gpio_set_level(MOTOR_B_IN1_GPIO, 0);
        gpio_set_level(MOTOR_B_IN2_GPIO, 0);
    }

    // Ustawienie PWM
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_B_PWM_CHANNEL, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_B_PWM_CHANNEL);
    }

    return ret;
}

/**
 * @brief Zatrzymuje oba silniki
 * 
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_stop(void)
{
    esp_err_t ret_a = motor_a_set_speed(0);
    esp_err_t ret_b = motor_b_set_speed(0);
    
    return (ret_a == ESP_OK && ret_b == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Ustawia prędkość obu silników jednocześnie
 * 
 * @param speed_a Prędkość silnika A (-255 do 255)
 * @param speed_b Prędkość silnika B (-255 do 255)
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_set_speeds(int16_t speed_a, int16_t speed_b)
{
    esp_err_t ret_a = motor_a_set_speed(speed_a);
    esp_err_t ret_b = motor_b_set_speed(speed_b);
    
    return (ret_a == ESP_OK && ret_b == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Sprawdza czy sterownik jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool motor_controller_is_initialized(void)
{
    return initialized;
}
