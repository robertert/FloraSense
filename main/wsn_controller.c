/**
 * @file wsn_controller.c
 * @brief Kontroler WSN (Wireless Sensor Network) - logika autonomicznego poruszania się
 * 
 * Logika:
 * 1. Jeśli światło jest lepsze gdzieś indziej, to tam przejeżdża
 * 2. Jeśli napotka przeszkodę (czujnik IR 2 z przodu), to się zatrzymuje zawsze
 */

#include "wsn_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "WSN_CONTROLLER";

static bool initialized = false;
static wsn_controller_config_t current_config = {0};

// Flagi bezpieczeństwa - wskazują czy przeszkoda jest wykryta w danym kierunku
static volatile bool obstacle_front_detected = false;  // Przeszkoda z przodu
static volatile bool obstacle_back_detected = false;   // Przeszkoda z tyłu
static portMUX_TYPE obstacle_flag_mutex = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Inicjalizuje kontroler WSN
 */
esp_err_t wsn_controller_init(const wsn_controller_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Konfiguracja WSN jest NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->light_sensor_1 == NULL || config->light_sensor_2 == NULL) {
        ESP_LOGE(TAG, "Konfiguracja czujników światła jest nieprawidłowa");
        return ESP_ERR_INVALID_ARG;
    }

    // Sprawdź czy czujniki światła są zainicjalizowane
    if (!sensor_light_is_initialized(config->light_sensor_1)) {
        ESP_LOGE(TAG, "Czujnik światła 1 nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    if (!sensor_light_is_initialized(config->light_sensor_2)) {
        ESP_LOGE(TAG, "Czujnik światła 2 nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    // Sprawdź czy czujnik IR jest zainicjalizowany
    if (!sensor_ir_is_initialized(config->ir_sensor_front)) {
        ESP_LOGE(TAG, "Czujnik IR z przodu (GPIO %d) nie jest zainicjalizowany", config->ir_sensor_front);
        return ESP_ERR_INVALID_STATE;
    }

    // Sprawdź czy sterownik silników jest zainicjalizowany
    if (!motor_controller_is_initialized()) {
        ESP_LOGE(TAG, "Sterownik silników nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(&current_config, config, sizeof(wsn_controller_config_t));
    initialized = true;

    ESP_LOGI(TAG, "Kontroler WSN zainicjalizowany");
    ESP_LOGI(TAG, "  Czujnik światła 1: Port=%d, SDA=%d, SCL=%d", 
             config->light_sensor_1->i2c_port, 
             config->light_sensor_1->sda_pin, 
             config->light_sensor_1->scl_pin);
    ESP_LOGI(TAG, "  Czujnik światła 2: Port=%d, SDA=%d, SCL=%d", 
             config->light_sensor_2->i2c_port, 
             config->light_sensor_2->sda_pin, 
             config->light_sensor_2->scl_pin);
    ESP_LOGI(TAG, "  Czujnik IR z przodu: GPIO %d", config->ir_sensor_front);
    ESP_LOGI(TAG, "  Bazowa prędkość: %d", config->base_speed);
    ESP_LOGI(TAG, "  Próg różnicy światła: %.2f lux", config->light_threshold_lux);
    ESP_LOGI(TAG, "  Interwał sprawdzania: %lu ms", config->check_interval_ms);
    ESP_LOGI(TAG, "  Dystans przesunięcia: %.2f cm", config->move_distance_cm);
    ESP_LOGI(TAG, "  Czas oczekiwania po progu: %lu ms (%.1f min)", 
             config->wait_after_threshold_ms, 
             config->wait_after_threshold_ms / 60000.0f);

    return ESP_OK;
}

/**
 * @brief Task kontrolera WSN - główna pętla logiki
 * 
 * Logika:
 * 1. Sprawdza różnicę światła
 * 2. Jeśli różnica > próg → przesuwa się o move_distance_cm w kierunku lepszego światła
 * 3. Po przesunięciu sprawdza ponownie
 * 4. Jeśli różnica < próg → zatrzymuje się i czeka wait_after_threshold_ms
 * 5. Po oczekiwaniu powtarza proces
 */
void wsn_controller_task(void *param)
{
    wsn_controller_config_t *config = (wsn_controller_config_t *)param;

    if (config == NULL) {
        ESP_LOGE(TAG, "Brak konfiguracji dla taska WSN");
        vTaskDelete(NULL);
        return;
    }

    // Inicjalizacja kontrolera
    esp_err_t ret = wsn_controller_init(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zainicjalizować kontrolera WSN: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Task kontrolera WSN uruchomiony");
    ESP_LOGI(TAG, "  Dystans przesunięcia: %.2f cm", config->move_distance_cm);
    ESP_LOGI(TAG, "  Czas oczekiwania po progu: %lu ms", config->wait_after_threshold_ms);

    sensor_light_reading_t light_1 = {0};
    sensor_light_reading_t light_2 = {0};
    bool waiting_mode = false;  // Tryb oczekiwania po osiągnięciu progu

    while (1) {
        // Uwaga: Sprawdzanie przeszkód jest obsługiwane przez osobny task obstacle_monitor_task
        // który ma najwyższy priorytet i zatrzymuje silniki niezależnie od tego taska
        
        // Jeśli jesteśmy w trybie oczekiwania, czekaj
        if (waiting_mode) {
            ESP_LOGI(TAG, "Tryb oczekiwania - czekam %lu ms przed następnym sprawdzeniem",
                     config->wait_after_threshold_ms);
            vTaskDelay(pdMS_TO_TICKS(config->wait_after_threshold_ms));
            waiting_mode = false;
            ESP_LOGI(TAG, "Koniec oczekiwania - rozpoczynam nowe sprawdzenie");
            continue;
        }

        // PRIORYTET 2: Sprawdź światło z obu czujników
        bool light_1_ok = false;
        bool light_2_ok = false;

        if (sensor_light_is_initialized(config->light_sensor_1)) {
            if (sensor_light_read(config->light_sensor_1, &light_1) == ESP_OK) {
                light_1_ok = true;
            } else {
                ESP_LOGW(TAG, "Błąd odczytu czujnika światła 1");
            }
        }

        if (sensor_light_is_initialized(config->light_sensor_2)) {
            if (sensor_light_read(config->light_sensor_2, &light_2) == ESP_OK) {
                light_2_ok = true;
            } else {
                ESP_LOGW(TAG, "Błąd odczytu czujnika światła 2");
            }
        }

        // Jeśli oba czujniki działają, podejmij decyzję
        if (light_1_ok && light_2_ok) {
            float diff = light_1.lux - light_2.lux;
            float abs_diff = fabsf(diff);

            ESP_LOGI(TAG, "Światło 1: %.2f lux, Światło 2: %.2f lux, Różnica: %.2f lux",
                     light_1.lux, light_2.lux, diff);

            // Jeśli różnica jest większa niż próg, przesuń się o move_distance_cm
            // Uwaga: light_sensor_1 = fizyczny czujnik z przodu, light_sensor_2 = fizyczny czujnik z tyłu
            if (abs_diff > config->light_threshold_lux) {
                const char *direction = NULL;
                
                if (diff > 0) {
                    // Światło 1 jest lepsze (przód) - przesuń się do przodu
                    direction = "forward";
                    ESP_LOGI(TAG, "Światło 1 lepsze (%.2f > %.2f lux) - przesuwam się %.2f cm do przodu",
                             light_1.lux, light_2.lux, config->move_distance_cm);
                } else {
                    // Światło 2 jest lepsze (tył) - przesuń się do tyłu
                    direction = "backward";
                    ESP_LOGI(TAG, "Światło 2 lepsze (%.2f > %.2f lux) - przesuwam się %.2f cm do tyłu",
                             light_2.lux, light_1.lux, config->move_distance_cm);
                }

                // Przesuń się o określony dystans
                ret = motor_controller_move_distance(direction, config->move_distance_cm, config->base_speed);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Błąd przesunięcia: %s", esp_err_to_name(ret));
                } else {
                    ESP_LOGI(TAG, "Przesunięcie zakończone - sprawdzam ponownie...");
                }

                // Krótkie opóźnienie po przesunięciu przed następnym sprawdzeniem
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                // Różnica jest mała - osiągnięto próg, przejdź w tryb oczekiwania
                ESP_LOGI(TAG, "Różnica światła zbyt mała (%.2f lux <= %.2f lux) - osiągnięto próg, przechodzę w tryb oczekiwania",
                         abs_diff, config->light_threshold_lux);
                motor_controller_stop();
                waiting_mode = true;
            }
        } else {
            // Jeśli któryś czujnik nie działa, zatrzymaj się
            ESP_LOGW(TAG, "Jeden z czujników światła nie działa - zatrzymywanie");
            motor_controller_stop();
        }

        // Czekaj przed następnym sprawdzeniem (jeśli nie jesteśmy w trybie oczekiwania)
        if (!waiting_mode) {
            vTaskDelay(pdMS_TO_TICKS(config->check_interval_ms));
        }
    }
}

/**
 * @brief Task monitorujący przeszkody - niezależna pętla kontrolująca czujniki IR
 * 
 * Ten task działa niezależnie i zatrzymuje silniki gdy wykryje przeszkodę,
 * niezależnie od sterowania MQTT lub WSN. Ma najwyższy priorytet bezpieczeństwa.
 * Sprawdza oba czujniki IR (przód i tył).
 */
void obstacle_monitor_task(void *param)
{
    obstacle_monitor_config_t *config = (obstacle_monitor_config_t *)param;
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Brak konfiguracji dla taska monitorowania przeszkód");
        vTaskDelete(NULL);
        return;
    }
    
    if (!sensor_ir_is_initialized(config->ir_sensor_front)) {
        ESP_LOGE(TAG, "Czujnik IR z przodu (GPIO %d) nie jest zainicjalizowany", config->ir_sensor_front);
        vTaskDelete(NULL);
        return;
    }
    
    if (!sensor_ir_is_initialized(config->ir_sensor_back)) {
        ESP_LOGE(TAG, "Czujnik IR z tyłu (GPIO %d) nie jest zainicjalizowany", config->ir_sensor_back);
        vTaskDelete(NULL);
        return;
    }
    
    if (!motor_controller_is_initialized()) {
        ESP_LOGE(TAG, "Sterownik silników nie jest zainicjalizowany");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Task monitorowania przeszkód uruchomiony");
    ESP_LOGI(TAG, "  Czujnik IR z przodu: GPIO %d", config->ir_sensor_front);
    ESP_LOGI(TAG, "  Czujnik IR z tyłu: GPIO %d", config->ir_sensor_back);
    
    bool obstacle_front = false;
    bool obstacle_back = false;
    uint32_t check_interval_ms = 50;  // Sprawdzaj co 50ms dla szybkiej reakcji
    
    while (1) {
        bool current_obstacle_front = false;
        bool current_obstacle_back = false;
        
        // Sprawdź czujnik z przodu
        int ir_front_state = sensor_ir_read(config->ir_sensor_front);
        if (ir_front_state < 0) {
            ESP_LOGW(TAG, "Błąd odczytu czujnika IR z przodu (GPIO %d)", config->ir_sensor_front);
        } else if (ir_front_state == 1) {
            // Wykryto przeszkodę z przodu
            current_obstacle_front = true;
            if (!obstacle_front) {
                ESP_LOGW(TAG, "⚠️ WYKRYTO PRZESZKODĘ Z PRZODU (GPIO %d)! Zatrzymywanie silników...", 
                         config->ir_sensor_front);
                obstacle_front = true;
            }
        } else {
            if (obstacle_front) {
                ESP_LOGI(TAG, "Przeszkoda z przodu zniknęła");
                obstacle_front = false;
            }
        }
        
        // Sprawdź czujnik z tyłu
        int ir_back_state = sensor_ir_read(config->ir_sensor_back);
        if (ir_back_state < 0) {
            ESP_LOGW(TAG, "Błąd odczytu czujnika IR z tyłu (GPIO %d)", config->ir_sensor_back);
        } else if (ir_back_state == 1) {
            // Wykryto przeszkodę z tyłu
            current_obstacle_back = true;
            if (!obstacle_back) {
                ESP_LOGW(TAG, "⚠️ WYKRYTO PRZESZKODĘ Z TYŁU (GPIO %d)! Zatrzymywanie silników...", 
                         config->ir_sensor_back);
                obstacle_back = true;
            }
        } else {
            if (obstacle_back) {
                ESP_LOGI(TAG, "Przeszkoda z tyłu zniknęła");
                obstacle_back = false;
            }
        }
        
        // Aktualizuj flagi bezpieczeństwa (thread-safe)
        portENTER_CRITICAL(&obstacle_flag_mutex);
        obstacle_front_detected = current_obstacle_front;
        obstacle_back_detected = current_obstacle_back;
        portEXIT_CRITICAL(&obstacle_flag_mutex);
        
        // Sprawdź aktualny kierunek ruchu i zatrzymaj tylko jeśli przeszkoda jest w tym kierunku
        char current_direction[16] = {0};
        if (motor_controller_get_current_direction(current_direction) == ESP_OK) {
            bool should_stop = false;
            
            if (strcmp(current_direction, "forward") == 0 && current_obstacle_front) {
                // Robot jedzie do przodu i przeszkoda jest z przodu → zatrzymaj
                should_stop = true;
                ESP_LOGW(TAG, "Przeszkoda z przodu podczas jazdy do przodu - zatrzymywanie");
            } else if (strcmp(current_direction, "backward") == 0 && current_obstacle_back) {
                // Robot jedzie do tyłu i przeszkoda jest z tyłu → zatrzymaj
                should_stop = true;
                ESP_LOGW(TAG, "Przeszkoda z tyłu podczas jazdy do tyłu - zatrzymywanie");
            }
            
            if (should_stop) {
                motor_controller_stop();
            }
        }
        
        if (current_obstacle_front || current_obstacle_back) {
            ESP_LOGD(TAG, "Przeszkoda aktywna (przód: %s, tył: %s, kierunek: %s)",
                     current_obstacle_front ? "TAK" : "NIE",
                     current_obstacle_back ? "TAK" : "NIE",
                     current_direction);
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    }
}

/**
 * @brief Sprawdza czy przeszkoda jest wykryta
 * 
 * Funkcja do sprawdzania przed włączeniem silników.
 * Jeśli przeszkoda jest wykryta, nie należy włączać silników.
 * 
 * @return true jeśli przeszkoda jest wykryta (NIE włączać silników)
 * @return false jeśli brak przeszkody (można włączyć silniki)
 */
bool obstacle_is_detected(const char *direction)
{
    bool front_detected, back_detected;
    
    portENTER_CRITICAL(&obstacle_flag_mutex);
    front_detected = obstacle_front_detected;
    back_detected = obstacle_back_detected;
    portEXIT_CRITICAL(&obstacle_flag_mutex);
    
    // Jeśli nie podano kierunku, sprawdź czy jakakolwiek przeszkoda
    if (direction == NULL) {
        return front_detected || back_detected;
    }
    
    // Sprawdź kierunek
    if (strcmp(direction, "forward") == 0 || strcmp(direction, "przód") == 0) {
        // Ruch do przodu - sprawdź przeszkodę z przodu
        return front_detected;
    } else if (strcmp(direction, "backward") == 0 || 
               strcmp(direction, "wstecz") == 0 || 
               strcmp(direction, "tył") == 0) {
        // Ruch do tyłu - sprawdź przeszkodę z tyłu
        return back_detected;
    }
    
    // Nieznany kierunek - sprawdź wszystkie przeszkody
    return front_detected || back_detected;
}

