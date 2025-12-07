#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "mqtt_client.h"     // oficjalny nagłówek ESP-IDF
#include "flora_mqtt.h"      // Twój własny nagłówek

#include "sensor_light.h"
#include "sensor_temp.h"
#include "sensor_soil.h"
#include "sensor_proximity.h"
#include "wifi.h"

static const char *TAG = "flora-mqtt";

/* ---- USTAW TO DO SWOJEJ SIECI ---- */
#define MQTT_BROKER_URI "mqtt://172.20.10.3:1883" 

/* ---- IDENTYFIKATORY UŻYTKOWNIK/URZĄDZENIE ---- */
#define USER_ID     "user1"
#define DEVICE_ID   "device123"

#define PUB_INTERVAL_MS 10000   // Publikacja co 10 sekund

static esp_mqtt_client_handle_t client = NULL;

/* -----------------------------------------
   Funkcja pomocnicza do publikacji danych
-------------------------------------------*/
static void publish_sensor(const char *topic_suffix, const char *payload)
{
    char topic[128];
    snprintf(topic, sizeof(topic),
             "florasense/%s/%s/%s",
             USER_ID, DEVICE_ID, topic_suffix);

    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Published to %s | msg_id=%d | %s", topic, msg_id, payload);
}

/* -----------------------------------------
   Handler zdarzeń MQTT (zgodny z ESP-IDF v5.5)
-------------------------------------------*/
static void mqtt_event_handler_cb(void *handler_args,
                                  esp_event_base_t base,
                                  int32_t event_id,
                                  void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
        {
            ESP_LOGI(TAG, "MQTT connected!");

            char water_cmd[128];
            char move_cmd[128];

            snprintf(water_cmd, sizeof(water_cmd),
                    "florasense/%s/%s/cmd/water",
                    USER_ID, DEVICE_ID);

            snprintf(move_cmd, sizeof(move_cmd),
                    "florasense/%s/%s/cmd/move",
                    USER_ID, DEVICE_ID);

            esp_mqtt_client_subscribe(client, water_cmd, 1);
            esp_mqtt_client_subscribe(client, move_cmd, 1);

            ESP_LOGI(TAG, "Subscribed to %s & %s", water_cmd, move_cmd);
        }
        break;

        case MQTT_EVENT_DATA:
        {
            ESP_LOGI(TAG, "[CMD] Topic: %.*s | Data: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            if (strstr(event->topic, "/cmd/water"))
            {
                ESP_LOGI(TAG, "WATER_CMD received: %.*s", event->data_len, event->data);
                // TODO: GPIO dla pompy
            }
            else if (strstr(event->topic, "/cmd/move"))
            {
                ESP_LOGI(TAG, "MOVE_CMD received: %.*s", event->data_len, event->data);
                // TODO: sterowanie silnikiem
            }
        }
        break;

        default:
            break;
    }
}

/* -----------------------------------------
   Start klienta MQTT
-------------------------------------------*/
void mqtt_app_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    client = esp_mqtt_client_init(&cfg);

    esp_mqtt_client_register_event(
        client, ESP_EVENT_ANY_ID,
        mqtt_event_handler_cb, NULL);

    esp_mqtt_client_start(client);
}

/* -----------------------------------------
   Task publikujący dane sensorów
-------------------------------------------*/
void mqtt_publish_task(void *pvParameters)
{
    while (1)
    {
        // Jeśli nie masz jeszcze implementacji czujników, użyj stałych
        float lux  = 123.4; // sensor_light_read();
        float temp = 24.0;  // sensor_temp_read();
        float soil = 55.0;  // sensor_soil_read();
        float prox = 6.0;   // sensor_proximity_read();
        int batt   = 75;    // mock

        char payload[128];

        snprintf(payload, sizeof(payload), "{\"value\": %.2f, \"unit\": \"lux\"}", lux);
        publish_sensor("sensor/light", payload);

        snprintf(payload, sizeof(payload), "{\"value\": %.2f, \"unit\": \"C\"}", temp);
        publish_sensor("sensor/temp", payload);

        snprintf(payload, sizeof(payload), "{\"value\": %.2f, \"unit\": \"%%\"}", soil);
        publish_sensor("sensor/soil", payload);

        snprintf(payload, sizeof(payload), "{\"value\": %.2f, \"unit\": \"raw\"}", prox);
        publish_sensor("sensor/proximity", payload);

        snprintf(payload, sizeof(payload), "{\"value\": %d, \"unit\": \"%%\"}", batt);
        publish_sensor("state/battery", payload);

        vTaskDelay(pdMS_TO_TICKS(PUB_INTERVAL_MS));
    }
}
