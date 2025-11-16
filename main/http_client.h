#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * @file http_client.h
 * @brief Obsługa żądań HTTP przez surowy socket TCP oraz esp_http_client
 */

/**
 * @brief Wykonuje żądanie HTTP GET z pełnym odbiorem odpowiedzi przez surowy socket TCP
 * @param host Nazwa hosta
 * @param port Port
 * @param path Ścieżka URL
 * @param response_buffer Bufor na odpowiedź
 * @param buffer_size Rozmiar bufora
 * @return Liczba odebranych bajtów lub -1 w przypadku błędu
 */
int http_get_raw_full(const char *host, int port, const char *path, 
                      char *response_buffer, size_t buffer_size);

/**
 * @brief Zadanie FreeRTOS wykonujące żądanie HTTP GET przez surowy socket TCP
 * @param pvParameters Parametr zadania (niewykorzystywany)
 */
void http_get_task_raw(void *pvParameters);

/**
 * @brief Zadanie FreeRTOS wykonujące żądanie HTTP GET przez esp_http_client
 * @param pvParameters Parametr zadania (niewykorzystywany)
 */
void http_get_task(void *pvParameters);

