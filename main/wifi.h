#pragma once

#include "esp_err.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

#include "config.h"

/**
 * @file wifi_module.h
 * @brief Obsługa połączenia Wi-Fi dla ESP32.
 */

// Eksportowany uchwyt grupy zdarzeń Wi-Fi
extern EventGroupHandle_t s_wifi_event_group;

/**
 * @brief Sprawdza czy WiFi jest połączone (thread-safe)
 * @return true jeśli połączone, false w przeciwnym razie
 */
bool wifi_is_connected(void);

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