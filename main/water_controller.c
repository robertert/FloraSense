/**
 * @file water_controller.c
 * @brief Kontroler automatycznego podlewania - jazda do przodu przy niskiej wilgotności gleby
 */

#include "water_controller.h"
#include "flora_mqtt.h"
#include "sensor_soil.h"
#include "sensor_ir.h"
#include "motor_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WATER_CONTROLLER";

#define WATER_CHECK_INTERVAL_MS 600000  // Sprawdzaj wilgotność co 10 minut
#define WATER_MOVE_DISTANCE_CM 5.0f   // Przesuń się o 5cm na raz
#define WATER_MOVE_SPEED 128           // Prędkość silników

/**
 * @brief Task kontrolera automatycznego podlewania
 * 
 * Logika:
 * 1. Sprawdza czy water_enabled jest włączone
 * 2. Sprawdza wilgotność gleby vs soil_humidity_threshold
 * 3. Gdy wilgotność < próg: jedzie do przodu aż do wykrycia przeszkody (czujnik IR)
 * 4. Po dotarciu do przeszkody: zatrzymuje się i czeka
 */
void water_controller_task(void *param)
{
    ESP_LOGI(TAG, "Task kontrolera automatycznego podlewania uruchomiony");

    while (1) {
        // Sprawdź czy automatyczne podlewanie jest włączone
        if (!mqtt_get_water_enabled()) {
            vTaskDelay(pdMS_TO_TICKS(WATER_CHECK_INTERVAL_MS));
            continue;
        }

        // Sprawdź wilgotność gleby
        sensor_soil_reading_t soil;
        if (sensor_soil_read(&soil) != ESP_OK) {
            ESP_LOGW(TAG, "Błąd odczytu wilgotności gleby");
            vTaskDelay(pdMS_TO_TICKS(WATER_CHECK_INTERVAL_MS));
            continue;
        }

        float threshold = mqtt_get_soil_humidity_threshold();
        ESP_LOGI(TAG, "Wilgotność gleby: %.2f%%, Próg: %.2f%%", soil.moisture_percent, threshold);

        // Jeśli wilgotność jest poniżej progu, jedź do przodu aż do ściany
        if (soil.moisture_percent < threshold) {
            ESP_LOGW(TAG, "Wilgotność gleby poniżej progu (%.2f%% < %.2f%%) - jadę do przodu do ściany",
                     soil.moisture_percent, threshold);

            // Sprawdź czy sterownik silników jest zainicjalizowany
            if (!motor_controller_is_initialized()) {
                ESP_LOGW(TAG, "Sterownik silników nie jest zainicjalizowany");
                vTaskDelay(pdMS_TO_TICKS(WATER_CHECK_INTERVAL_MS));
                continue;
            }

            // Jedź do przodu aż do wykrycia przeszkody
            bool obstacle_detected = false;
            gpio_num_t ir_sensor_front = mqtt_get_ir_sensor_pin_2();  // Czujnik IR z przodu

            while (!obstacle_detected) {
                // Sprawdź przeszkodę przed ruchem
                if (sensor_ir_is_initialized(ir_sensor_front)) {
                    int ir_state = sensor_ir_read(ir_sensor_front);
                    if (ir_state > 0) {
                        ESP_LOGI(TAG, "Wykryto przeszkodę (ścianę) - zatrzymuję się");
                        motor_controller_stop();
                        obstacle_detected = true;
                        break;
                    }
                }

                // Jedź do przodu o mały dystans
                esp_err_t ret = motor_controller_move_distance("forward", WATER_MOVE_DISTANCE_CM, WATER_MOVE_SPEED);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Błąd przesunięcia: %s", esp_err_to_name(ret));
                    break;
                }

                // Krótkie opóźnienie przed następnym sprawdzeniem
                vTaskDelay(pdMS_TO_TICKS(200));

                // Sprawdź ponownie przeszkodę po ruchu
                if (sensor_ir_is_initialized(ir_sensor_front)) {
                    int ir_state = sensor_ir_read(ir_sensor_front);
                    if (ir_state > 0) {
                        ESP_LOGI(TAG, "Wykryto przeszkodę (ścianę) po ruchu - zatrzymuję się");
                        motor_controller_stop();
                        obstacle_detected = true;
                        break;
                    }
                }
            }

            if (obstacle_detected) {
                ESP_LOGI(TAG, "Dotarłem do ściany - czekam %lu ms przed następnym sprawdzeniem", WATER_CHECK_INTERVAL_MS);
                // Tutaj można dodać logikę podlewania w przyszłości
            }
        } else {
            ESP_LOGI(TAG, "Wilgotność gleby OK (%.2f%% >= %.2f%%)", soil.moisture_percent, threshold);
        }

        // Czekaj przed następnym sprawdzeniem
        vTaskDelay(pdMS_TO_TICKS(WATER_CHECK_INTERVAL_MS));
    }
}

