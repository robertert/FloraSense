#pragma once

#include "esp_err.h"
#include "sensor_light.h"
#include "sensor_ir.h"
#include "motor_controller.h"

/**
 * @brief Konfiguracja kontrolera WSN
 */
typedef struct {
    sensor_light_config_t *light_sensor_1;  // Czujnik światła 1
    sensor_light_config_t *light_sensor_2;  // Czujnik światła 2
    gpio_num_t ir_sensor_front;            // Czujnik IR z przodu (nr 2)
    int16_t base_speed;                    // Bazowa prędkość silników (0-255)
    float light_threshold_lux;             // Próg różnicy światła w lux (aby uniknąć ciągłych zmian)
    uint32_t check_interval_ms;            // Interwał sprawdzania czujników w ms
    float move_distance_cm;                // Dystans przesunięcia w cm (np. 5cm)
    uint32_t wait_after_threshold_ms;      // Czas oczekiwania po osiągnięciu progu w ms (np. 5 min)
} wsn_controller_config_t;

/**
 * @brief Inicjalizuje kontroler WSN
 * 
 * @param config Konfiguracja kontrolera WSN
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t wsn_controller_init(const wsn_controller_config_t *config);

/**
 * @brief Task kontrolera WSN - główna pętla logiki
 * 
 * @param param Wskaźnik do konfiguracji wsn_controller_config_t
 */
void wsn_controller_task(void *param);

/**
 * @brief Task monitorujący przeszkody - niezależna pętla kontrolująca czujniki IR
 * 
 * Ten task działa niezależnie i zatrzymuje silniki gdy wykryje przeszkodę,
 * niezależnie od sterowania MQTT lub WSN. Ma najwyższy priorytet bezpieczeństwa.
 * Sprawdza oba czujniki IR (przód i tył).
 * 
 * @param param Wskaźnik do struktury z pinami czujników IR (przód i tył)
 */
typedef struct {
    gpio_num_t ir_sensor_front;  // Czujnik IR z przodu
    gpio_num_t ir_sensor_back;   // Czujnik IR z tyłu
} obstacle_monitor_config_t;

void obstacle_monitor_task(void *param);

/**
 * @brief Sprawdza czy przeszkoda jest wykryta w danym kierunku
 * 
 * Funkcja do sprawdzania przed włączeniem silników.
 * 
 * @param direction Kierunek: "forward" lub "backward" (lub NULL dla sprawdzenia ogólnego)
 * @return true jeśli przeszkoda jest wykryta w danym kierunku (NIE włączać silników)
 * @return false jeśli brak przeszkody w danym kierunku (można włączyć silniki)
 */
bool obstacle_is_detected(const char *direction);

/**
 * @brief Wykonuje pojedyncze sprawdzenie światła i ruch w kierunku lepszego światła
 * 
 * Funkcja wykonuje jedno sprawdzenie światła z obu czujników i przesuwa urządzenie
 * w kierunku lepszego światła, jeśli różnica przekracza próg. Po wykonaniu ruchu
 * uruchamia pętlę kontynuującą sprawdzanie i poruszanie się aż do osiągnięcia progu
 * światła lub wyłączenia automatycznego poruszania się.
 * 
 * @return ESP_OK jeśli wykonano ruch i uruchomiono pętlę sprawdzania, ESP_FAIL jeśli błąd lub różnica za mała
 */
esp_err_t wsn_controller_single_light_search(void);
