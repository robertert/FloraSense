#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "mqtt_client.h"
#include "flora_mqtt.h"

#include "sensor_light.h"
#include "sensor_temp.h"
#include "sensor_soil.h"
#include "sensor_proximity.h"
#include "sensor_hall.h"
#include "sensor_ir.h"
#include "sensor_dock.h"
#include "wifi.h"

static const char *TAG = "flora-mqtt";

/* ---- BROKER ---- */
#define MQTT_BROKER_URI "mqtt://172.20.10.3:1883"

/* ---- DYNAMICZNE ID ---- */
static char user_id[32] = "default_user";   // ustawiane przez MQTT + NVS
static char device_id[32] = {0};            // generowane z MAC

#define PUB_INTERVAL_MS 10000

static esp_mqtt_client_handle_t client = NULL;

/* -------------------------------------------------------
   NVS: zapis i odczyt USER_ID
--------------------------------------------------------*/
static void load_user_id_from_nvs()
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(user_id);
        if (nvs_get_str(handle, "user_id", user_id, &size) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded USER_ID from NVS: %s", user_id);
        }
        nvs_close(handle);
    }
}

static void save_user_id_to_nvs(const char *new_id)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "user_id", new_id);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Saved USER_ID to NVS: %s", new_id);
    }
}

/* -------------------------------------------------------
   Publikacja danych
--------------------------------------------------------*/
static void publish_sensor(const char *topic_suffix, const char *payload)
{
    char topic[128];
    snprintf(topic, sizeof(topic),
             "florasense/%s/%s/%s",
             user_id, device_id, topic_suffix);

    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Published to %s | msg_id=%d | %s", topic, msg_id, payload);
}

/* -------------------------------------------------------
   Handler MQTT
--------------------------------------------------------*/
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

            /* --- SUBSKRYPCJA KOMEND --- */
            char water_cmd[128];
            char move_cmd[128];
            char user_cfg[128];

            snprintf(water_cmd, sizeof(water_cmd),
                     "florasense/%s/%s/cmd/water",
                     user_id, device_id);

            snprintf(move_cmd, sizeof(move_cmd),
                     "florasense/%s/%s/cmd/move",
                     user_id, device_id);

            snprintf(user_cfg, sizeof(user_cfg),
                     "florasense/%s/config/user",
                     device_id);

            esp_mqtt_client_subscribe(client, water_cmd, 1);
            esp_mqtt_client_subscribe(client, move_cmd, 1);
            esp_mqtt_client_subscribe(client, user_cfg, 1);

            ESP_LOGI(TAG, "Subscribed to: %s, %s, %s",
                     water_cmd, move_cmd, user_cfg);
        }
        break;

        case MQTT_EVENT_DATA:
        {
            ESP_LOGI(TAG, "[CMD] Topic: %.*s | Data: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            /* --- USTAWIANIE USER_ID PRZEZ MQTT --- */
            if (strstr(event->topic, "/config/user"))
            {
                char new_user[32] = {0};
                memcpy(new_user, event->data, event->data_len);

                ESP_LOGI(TAG, "Received USER_ID config: %s", new_user);

                save_user_id_to_nvs(new_user);
                strncpy(user_id, new_user, sizeof(user_id));
            }

            /* --- KOMENDA WATER --- */
            else if (strstr(event->topic, "/cmd/water"))
            {
                ESP_LOGI(TAG, "WATER_CMD received: %.*s",
                         event->data_len, event->data);
            }

            /* --- KOMENDA MOVE --- */
            else if (strstr(event->topic, "/cmd/move"))
            {
                ESP_LOGI(TAG, "MOVE_CMD received: %.*s",
                         event->data_len, event->data);
            }
        }
        break;

        default:
            break;
    }
}

/* -------------------------------------------------------
   Start klienta MQTT
--------------------------------------------------------*/
void mqtt_app_start(void)
{
    /* --- GENEROWANIE DEVICE_ID Z MAC --- */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(device_id, sizeof(device_id),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Generated DEVICE_ID: %s", device_id);

    /* --- ŁADOWANIE USER_ID Z NVS --- */
    load_user_id_from_nvs();

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    client = esp_mqtt_client_init(&cfg);

    esp_mqtt_client_register_event(
        client, ESP_EVENT_ANY_ID,
        mqtt_event_handler_cb, NULL);

    esp_mqtt_client_start(client);
}

/* -------------------------------------------------------
   Task publikujący dane
--------------------------------------------------------*/
void mqtt_publish_task(void *pvParameters)
{
    while (1)
    {
        char payload[128];

        /* --- ŚWIATŁO --- */
        sensor_light_reading_t light;
        if (sensor_light_read(&light) == ESP_OK) {
            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"lux\"}", light.lux);
            publish_sensor("sensor/light", payload);
        }

        /* --- TEMPERATURA + WILGOTNOŚĆ --- */
        sensor_temp_reading_t temp;
        if (sensor_temp_read(&temp) == ESP_OK) {
            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"C\"}", temp.temperature_c);
            publish_sensor("sensor/temp", payload);

            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"%%\"}", temp.humidity_percent);
            publish_sensor("sensor/humidity", payload);
        }

        /* --- GLEBA --- */
        sensor_soil_reading_t soil;
        if (sensor_soil_read(&soil) == ESP_OK) {
            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"%%\", \"mv\": %d}",
                     soil.moisture_percent, soil.millivolts);
            publish_sensor("sensor/soil", payload);
        }

        /* --- DOCK --- */
        if (sensor_dock_is_initialized()) {
            int dock_state = sensor_dock_read();
            snprintf(payload, sizeof(payload),
                     "{\"state\": \"%s\"}",
                     dock_state ? "connected" : "disconnected");
            publish_sensor("state/dock", payload);
        }

        /* --- HALL --- */
        if (sensor_hall_is_initialized()) {
            int hall_state = sensor_hall_read();
            snprintf(payload, sizeof(payload),
                     "{\"magnetic\": %s}",
                     hall_state ? "true" : "false");
            publish_sensor("sensor/hall", payload);
        }

        /* --- IR --- */
        if (sensor_ir_is_initialized()) {
            int ir_state = sensor_ir_read();
            snprintf(payload, sizeof(payload),
                     "{\"obstacle\": %s}",
                     ir_state ? "true" : "false");
            publish_sensor("sensor/ir", payload);
        }

        /* --- BATERIA (mock) --- */
        int batt = 75;
        snprintf(payload, sizeof(payload),
                 "{\"value\": %d, \"unit\": \"%%\"}", batt);
        publish_sensor("state/battery", payload);

        vTaskDelay(pdMS_TO_TICKS(PUB_INTERVAL_MS));
    }
}
