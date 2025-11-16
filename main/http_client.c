#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "http_client.h"
#include "wifi.h"

static const char *TAG = "http_client";

/**
 * @brief Event handler dla esp_http_client
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            printf("Odpowiedź: %.*s\n", evt->data_len, (char*)evt->data);
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Wykonuje żądanie HTTP GET z pełnym odbiorem odpowiedzi
 * @param host Nazwa hosta
 * @param port Port
 * @param path Ścieżka URL
 * @param response_buffer Bufor na odpowiedź
 * @param buffer_size Rozmiar bufora
 * @return Liczba odebranych bajtów lub -1 w przypadku błędu
 */
int http_get_raw_full(const char *host, int port, const char *path, 
                      char *response_buffer, size_t buffer_size)
{
    // Sprawdzenie poprawności parametrów
    if (host == NULL || path == NULL || response_buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Nieprawidłowe parametry funkcji http_get_raw_full");
        return -1;
    }
    
    int sock = -1;
    struct addrinfo hints, *result, *rp;
    char request[512];
    int total_bytes = 0;
    int bytes_received;
    char port_str[6];
    
    // Utworzenie socketu
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Błąd utworzenia socketu: %d", errno);
        return -1;
    }
    
    // Przygotowanie struktury hints dla getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    
    // Konwersja portu na string
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    // DNS lookup używając getaddrinfo
    int ret = getaddrinfo(host, port_str, &hints, &result);
    if (ret != 0) {
        ESP_LOGE(TAG, "Błąd DNS: nie można rozwiązać %s (kod błędu: %d)", host, ret);
        close(sock);
        return -1;
    }
    
    // Próba połączenia z pierwszym dostępnym adresem
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            // Połączenie udane
            break;
        }
        ESP_LOGW(TAG, "Nie udało się połączyć, próba następnego adresu...");
    }
    
    // Zwolnienie struktury addrinfo
    freeaddrinfo(result);
    
    if (rp == NULL) {
        ESP_LOGE(TAG, "Nie udało się połączyć z żadnym adresem");
        close(sock);
        return -1;
    }
    
    ESP_LOGI(TAG, "Połączono z serwerem %s:%d", host, port);
    
    // Przygotowanie i wysłanie żądania
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: ESP32\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);
    
    if (send(sock, request, strlen(request), 0) < 0) {
        ESP_LOGE(TAG, "Błąd wysyłania: %d", errno);
        close(sock);
        return -1;
    }
    
    // Odbieranie odpowiedzi w pętli
    memset(response_buffer, 0, buffer_size);
    while (total_bytes < (buffer_size - 1)) {
        bytes_received = recv(sock, 
                             response_buffer + total_bytes, 
                             buffer_size - total_bytes - 1, 
                             0);
        
        if (bytes_received < 0) {
            ESP_LOGE(TAG, "Błąd odbierania: %d", errno);
            close(sock);
            return -1;
        }
        
        if (bytes_received == 0) {
            // Serwer zamknął połączenie
            break;
        }
        
        total_bytes += bytes_received;
    }
    
    response_buffer[total_bytes] = '\0';
    close(sock);
    
    return total_bytes;
}

void http_get_task_raw(void *pvParameters)
{
    // Czekaj na połączenie WiFi
    while (!wifi_is_connected()) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Czekanie na połączenie WiFi...");
    }
    
    ESP_LOGI(TAG, "Wykonywanie żądania HTTP przez surowy socket...");

    // Przykład: Z pełnym buforem odpowiedzi - użyj heap zamiast stacku
    const size_t response_size = 4096;
    char *response = (char*)malloc(response_size);
    if (response == NULL) {
        ESP_LOGE(TAG, "Błąd alokacji pamięci dla bufora odpowiedzi");
        vTaskDelete(NULL);
        return;
    }
    
    int bytes = http_get_raw_full("example.com", 80, "/", response, response_size);
    if (bytes > 0) {
        ESP_LOGI(TAG, "Otrzymano %d bajtów odpowiedzi", bytes);
        printf("Odpowiedź: %s", response);
    } else {
        ESP_LOGE(TAG, "Błąd odbierania odpowiedzi: %d", bytes);
    }
    
    free(response);
    vTaskDelete(NULL);
}

void http_get_task(void *pvParameters)
{
    esp_http_client_config_t config = {
        .url = "http://example.com/",
        .event_handler = _http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(!wifi_is_connected()) {
        ESP_LOGE(TAG, "Brak połączenia WiFi. Zadanie HTTP GET zakończone.");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET zakończony sukcesem, kod: %d",
                 esp_http_client_get_status_code(client));
        ESP_LOGI(TAG, "Długość odpowiedzi: %d", esp_http_client_get_content_length(client));
        for(int i = 0; i < esp_http_client_get_content_length(client); i++) {
            char buffer[2];
            esp_http_client_read(client, buffer, 1);
            buffer[1] = '\0';
            printf("%s", buffer);
        }
    } else {
        ESP_LOGE(TAG, "Błąd HTTP GET: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

