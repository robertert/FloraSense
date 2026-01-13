#pragma once

#include "esp_err.h"

/**
 * @brief Jedzie do tyłu aż do wykrycia przeszkody (ściany)
 * 
 * Funkcja porusza urządzenie do tyłu małymi krokami (5cm) aż do wykrycia przeszkody
 * przez czujnik IR z tyłu. Po dotarciu do ściany zatrzymuje silniki.
 * 
 * @return ESP_OK jeśli dotarło do ściany, ESP_FAIL w przypadku błędu
 */
esp_err_t water_controller_move_to_wall(void);

/**
 * @brief Task kontrolera automatycznego podlewania
 * 
 * Task sprawdza wilgotność gleby i gdy jest poniżej progu,
 * jedzie do tyłu aż do wykrycia przeszkody (ściany).
 * 
 * @param param Nieużywany (może być NULL)
 */
void water_controller_task(void *param);

