#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Inicjalizuje sterownik silników
 * 
 * Konfiguruje piny GPIO dla kierunku i PWM dla prędkości.
 * 
 * @return ESP_OK jeśli inicjalizacja się powiodła
 */
esp_err_t motor_controller_init(void);

/**
 * @brief Ustawia prędkość i kierunek silnika A
 * 
 * @param speed Prędkość w zakresie -255 do 255 (ujemne = wstecz, dodatnie = przód, 0 = stop)
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_a_set_speed(int16_t speed);

/**
 * @brief Ustawia prędkość i kierunek silnika B
 * 
 * @param speed Prędkość w zakresie -255 do 255 (ujemne = wstecz, dodatnie = przód, 0 = stop)
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_b_set_speed(int16_t speed);

/**
 * @brief Zatrzymuje oba silniki
 * 
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_stop(void);

/**
 * @brief Ustawia prędkość obu silników jednocześnie
 * 
 * @param speed_a Prędkość silnika A (-255 do 255)
 * @param speed_b Prędkość silnika B (-255 do 255)
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_set_speeds(int16_t speed_a, int16_t speed_b);

/**
 * @brief Sprawdza czy sterownik jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany, false w przeciwnym razie
 */
bool motor_controller_is_initialized(void);

/**
 * @brief Przejeżdża określoną odległość w danym kierunku
 * 
 * @param direction Kierunek: "forward", "backward", "left", "right"
 * @param distance_cm Odległość w centymetrach
 * @param speed Prędkość silników (0-255), domyślnie 128
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_move_distance(const char *direction, float distance_cm, int16_t speed);

/**
 * @brief Pobiera aktualny kierunek ruchu silników
 * 
 * @param direction Bufor na kierunek (min. 16 znaków): "forward", "backward", "stop", "unknown"
 * @return ESP_OK jeśli sukces
 */
esp_err_t motor_controller_get_current_direction(char *direction);
