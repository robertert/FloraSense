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
#include "wsn_controller.h"
#include <string.h>
#include <stdlib.h>  // dla abs()

static const char *TAG = "MOTOR_CTRL";

static bool initialized = false;

// Aktualny kierunek ruchu (thread-safe)
static volatile int16_t current_speed_a = 0;
static volatile int16_t current_speed_b = 0;
static portMUX_TYPE current_speed_mutex = portMUX_INITIALIZER_UNLOCKED;

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

    // Aktualizuj aktualną prędkość (thread-safe)
    portENTER_CRITICAL(&current_speed_mutex);
    current_speed_a = speed;
    portEXIT_CRITICAL(&current_speed_mutex);

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

    // Aktualizuj aktualną prędkość (thread-safe)
    if (ret == ESP_OK) {
        portENTER_CRITICAL(&current_speed_mutex);
        current_speed_b = speed;
        portEXIT_CRITICAL(&current_speed_mutex);
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
 * @return ESP_OK jeśli sukces, ESP_ERR_INVALID_STATE jeśli przeszkoda wykryta
 */
esp_err_t motor_controller_set_speeds(int16_t speed_a, int16_t speed_b)
{
    // Jeśli prędkość to 0 (stop), zawsze pozwól na zatrzymanie
    if (speed_a == 0 && speed_b == 0) {
        esp_err_t ret_a = motor_a_set_speed(speed_a);
        esp_err_t ret_b = motor_b_set_speed(speed_b);
        return (ret_a == ESP_OK && ret_b == ESP_OK) ? ESP_OK : ESP_FAIL;
    }
    
    // Określ kierunek na podstawie prędkości
    // Jeśli oba silniki mają dodatnią prędkość → ruch do przodu
    // Jeśli oba silniki mają ujemną prędkość → ruch do tyłu
    const char *direction = NULL;
    if (speed_a > 0 && speed_b > 0) {
        direction = "forward";  // Ruch do przodu
    } else if (speed_a < 0 && speed_b < 0) {
        direction = "backward";  // Ruch do tyłu
    }
    // Jeśli prędkości są różne (skręcanie), sprawdź wszystkie przeszkody
    
    // Sprawdź flagę bezpieczeństwa przed włączeniem silników
    if (obstacle_is_detected(direction)) {
        ESP_LOGW(TAG, "Przeszkoda wykryta w kierunku '%s' - blokada włączenia silników", 
                 direction ? direction : "nieznany");
        motor_controller_stop();  // Upewnij się, że silniki są zatrzymane
        return ESP_ERR_INVALID_STATE;
    }
    
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

/**
 * @brief Przejeżdża określoną odległość w danym kierunku
 * 
 * Kalibracja: ustaw MOTOR_CM_PER_SECOND w config.h na podstawie testów
 * 
 * @param direction Kierunek: "forward", "backward", "left", "right"
 * @param distance_cm Odległość w centymetrach
 * @param speed Prędkość silników (0-255), domyślnie 128
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_move_distance(const char *direction, float distance_cm, int16_t speed)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Sterownik silników nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    if (distance_cm <= 0) {
        ESP_LOGW(TAG, "Nieprawidłowa odległość: %.2f cm", distance_cm);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Sprawdź flagę bezpieczeństwa przed rozpoczęciem ruchu (z uwzględnieniem kierunku)
    if (obstacle_is_detected(direction)) {
        ESP_LOGW(TAG, "Przeszkoda wykryta w kierunku '%s' - blokada ruchu", direction);
        return ESP_ERR_INVALID_STATE;
    }

    // Kalibracja: cm/sekunda przy danej prędkości
    // Wartość z config.h - użytkownik powinien to skalibrować na podstawie testów
    const float cm_per_second = MOTOR_CM_PER_SECOND_128;
    
    // Oblicz czas jazdy w milisekundach
    float time_seconds = distance_cm / cm_per_second;
    uint32_t time_ms = (uint32_t)(time_seconds * 1000.0f);
    
    ESP_LOGI(TAG, "Przejeżdżanie %.2f cm w kierunku '%s' (prędkość=%d, czas=%lu ms, kalibracja=%.2f cm/s)",
             distance_cm, direction, speed, time_ms, cm_per_second);

    // Ustaw kierunek i prędkość
    if (strcmp(direction, "forward") == 0 || strcmp(direction, "przód") == 0) {
        if (motor_controller_set_speeds(speed, speed) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;  // Przeszkoda wykryta
        }
    } else if (strcmp(direction, "backward") == 0 || strcmp(direction, "wstecz") == 0 || strcmp(direction, "tył") == 0) {
        if (motor_controller_set_speeds(-speed, -speed) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;  // Przeszkoda wykryta
        }
    }
    else {
        ESP_LOGE(TAG, "Nieznany kierunek: %s", direction);
        return ESP_ERR_INVALID_ARG;
    }

    // Czekaj przez obliczony czas, ale sprawdzaj przeszkody w trakcie
    uint32_t elapsed_ms = 0;
    uint32_t check_interval = 50;  // Sprawdzaj co 50ms
    
    while (elapsed_ms < time_ms) {
        // Sprawdź czy przeszkoda pojawiła się podczas ruchu (z uwzględnieniem kierunku)
        if (obstacle_is_detected(direction)) {
            ESP_LOGW(TAG, "Przeszkoda wykryta podczas ruchu w kierunku '%s' - zatrzymywanie", direction);
            motor_controller_stop();
            return ESP_ERR_INVALID_STATE;
        }
        
        uint32_t delay_time = (time_ms - elapsed_ms > check_interval) ? check_interval : (time_ms - elapsed_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_time));
        elapsed_ms += delay_time;
    }

    // Zatrzymaj silniki
    motor_controller_stop();
    
    ESP_LOGI(TAG, "Przejazd zakończony");
    return ESP_OK;
}

/**
 * @brief Pobiera aktualny kierunek ruchu silników
 * 
 * @param direction Bufor na kierunek (min. 16 znaków): "forward", "backward", "stop", "unknown"
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_get_current_direction(char *direction)
{
    if (direction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t speed_a, speed_b;
    
    portENTER_CRITICAL(&current_speed_mutex);
    speed_a = current_speed_a;
    speed_b = current_speed_b;
    portEXIT_CRITICAL(&current_speed_mutex);

    // Określ kierunek na podstawie prędkości
    if (speed_a == 0 && speed_b == 0) {
        strncpy(direction, "stop", 16);
    } else if (speed_a > 0 && speed_b > 0) {
        strncpy(direction, "forward", 16);
    } else if (speed_a < 0 && speed_b < 0) {
        strncpy(direction, "backward", 16);
    } else {
        // Różne prędkości (skręcanie) - określ główny kierunek
        if (abs(speed_a) > abs(speed_b)) {
            strncpy(direction, speed_a > 0 ? "forward" : "backward", 16);
        } else {
            strncpy(direction, speed_b > 0 ? "forward" : "backward", 16);
        }
    }
    
    direction[15] = '\0';  // Zapewnij null terminator
    return ESP_OK;
}
