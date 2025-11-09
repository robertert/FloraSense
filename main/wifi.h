#pragma once

#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"

#include "config.h"

/**
 * @file wifi_module.h
 * @brief Obsługa połączenia Wi-Fi oraz żądań HTTP dla ESP32.
 */

// Eksportowany uchwyt grupy zdarzeń Wi-Fi
extern EventGroupHandle_t s_wifi_event_group;

// Zmienna statusu połączenia
extern bool wifi_connected;

/**
 * @brief Inicjalizuje Wi-Fi w trybie stacji (STA).
 */
void wifi_init_sta(void);

/**
 * @brief Inicjalizuje cały moduł Wi-Fi, w tym NVS i zadania FreeRTOS.
 */
void wifi_init(void);

/**
 * @brief Funkcja sprawdzająca aktualne połączenie Wi-Fi.
 */
void check_wifi_connection(void);

/**
 * @brief Zadanie FreeRTOS monitorujące połączenie Wi-Fi.
 * @param pvParameters Parametr zadania (niewykorzystywany).
 */
void wifi_status_task(void *pvParameters);

/**
 * @brief Zadanie FreeRTOS wykonujące żądanie HTTP GET.
 * @param pvParameters Parametr zadania (niewykorzystywany).
 */
void http_get_task(void *pvParameters);