/**
 * @file water_controller.c
 * @brief Kontroler automatycznego podlewania - jazda do tyłu przy niskiej wilgotności gleby
 */

#include "water_controller.h"
#include "flora_mqtt.h"
#include "sensor_soil.h"
#include "sensor_ir.h"
#include "motor_controller.h"
#include "ble_dock_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WATER_CONTROLLER";

#define WATER_MOVE_DISTANCE_CM 5.0f   // Przesuń się o 5cm na raz
#define WATER_MOVE_SPEED 128           // Prędkość silników
// WATER_CHECK_INTERVAL_MS jest teraz pobierany z MQTT przez mqtt_get_water_check_interval_ms()

/**
 * @brief Jedzie do tyłu aż do wykrycia przeszkody (ściany)
 * 
 * Funkcja porusza urządzenie do tyłu małymi krokami aż do wykrycia przeszkody
 * przez czujnik IR z tyłu. Po dotarciu do ściany zatrzymuje silniki.
 * 
 * @return ESP_OK jeśli dotarło do ściany, ESP_FAIL w przypadku błędu
 */
esp_err_t water_controller_move_to_wall(void)
{
    // Sprawdź czy sterownik silników jest zainicjalizowany
    if (!motor_controller_is_initialized()) {
        ESP_LOGW(TAG, "Sterownik silników nie jest zainicjalizowany");
        return ESP_FAIL;
    }

    // Jedź do tyłu aż do wykrycia przeszkody
    bool obstacle_detected = false;
    gpio_num_t ir_sensor_back = mqtt_get_ir_sensor_pin_1();  // Czujnik IR z tyłu

    while (!obstacle_detected) {
        // Sprawdź przeszkodę przed ruchem
        if (sensor_ir_is_initialized(ir_sensor_back)) {
            int ir_state = sensor_ir_read(ir_sensor_back);
            if (ir_state > 0) {
                ESP_LOGI(TAG, "Wykryto przeszkodę (ścianę) - zatrzymuję się");
                motor_controller_stop();
                obstacle_detected = true;
                break;
            }
        }

        // Jedź do tyłu o mały dystans
        esp_err_t ret = motor_controller_move_distance("backward", WATER_MOVE_DISTANCE_CM, WATER_MOVE_SPEED);
        if (ret == ESP_ERR_INVALID_STATE) {
            // Przeszkoda wykryta podczas ruchu - to jest sukces, dotarliśmy do ściany
            ESP_LOGI(TAG, "Przeszkoda wykryta podczas ruchu - dotarłem do ściany");
            motor_controller_stop();
            obstacle_detected = true;
            break;
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Błąd przesunięcia: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }

        // Krótkie opóźnienie przed następnym sprawdzeniem
        vTaskDelay(pdMS_TO_TICKS(200));

        // Sprawdź ponownie przeszkodę po ruchu
        if (sensor_ir_is_initialized(ir_sensor_back)) {
            int ir_state = sensor_ir_read(ir_sensor_back);
            if (ir_state > 0) {
                ESP_LOGI(TAG, "Wykryto przeszkodę (ścianę) po ruchu - zatrzymuję się");
                motor_controller_stop();
                obstacle_detected = true;
                break;
            }
        }
    }

    if (obstacle_detected) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

/**
 * @brief Task kontrolera automatycznego podlewania
 * 
 * Logika:
 * 1. Sprawdza czy water_enabled jest włączone
 * 2. Sprawdza wilgotność gleby vs soil_humidity_threshold
 * 3. Gdy wilgotność < próg: jedzie do tyłu aż do wykrycia przeszkody (czujnik IR)
 * 4. Po dotarciu do przeszkody: zatrzymuje się i czeka
 */
void water_controller_task(void *param)
{
    ESP_LOGI(TAG, "Task kontrolera automatycznego podlewania uruchomiony");

    while (1) {
        // Pobierz interwał z konfiguracji MQTT
        uint32_t check_interval = mqtt_get_water_check_interval_ms();
        
        // Sprawdź czy automatyczne podlewanie jest włączone
        if (!mqtt_get_water_enabled()) {
            ESP_LOGI(TAG, "Automatyczne podlewanie jest wyłączone");
            vTaskDelay(pdMS_TO_TICKS(check_interval));
            continue;
        }

        // Sprawdź wilgotność gleby
        sensor_soil_reading_t soil;
        if (sensor_soil_read(&soil) != ESP_OK) {
            ESP_LOGW(TAG, "Błąd odczytu wilgotności gleby");
            vTaskDelay(pdMS_TO_TICKS(check_interval));
            continue;
        }

        float threshold = mqtt_get_soil_humidity_threshold();
        ESP_LOGI(TAG, "Wilgotność gleby: %.2f%%, Próg: %.2f%%", soil.moisture_percent, threshold);

        // Jeśli wilgotność jest poniżej progu, jedź do tyłu aż do ściany
        if (soil.moisture_percent < threshold) {
            ESP_LOGW(TAG, "Wilgotność gleby poniżej progu (%.2f%% < %.2f%%) - jadę do tyłu do ściany",
                     soil.moisture_percent, threshold);

            esp_err_t move_res = water_controller_move_to_wall();
            if (move_res == ESP_OK) {
                // Z pętli water_controller używamy domyślnego czasu 100ms
                esp_err_t res = ble_dock_run_watering_cycle(100);
                ESP_LOGI(TAG, "Cykl podlewania zakończony z wynikiem: %s", esp_err_to_name(res));
        
                ESP_LOGI(TAG, "Dotarłem do ściany - czekam %lu ms przed następnym sprawdzeniem", check_interval);
            } else {
                ESP_LOGE(TAG, "Błąd podczas jazdy do ściany: %s", esp_err_to_name(move_res));
            }
        } else {
            ESP_LOGI(TAG, "Wilgotność gleby OK (%.2f%% >= %.2f%%)", soil.moisture_percent, threshold);
        }

        // Czekaj przed następnym sprawdzeniem
        vTaskDelay(pdMS_TO_TICKS(check_interval));
    }
}

