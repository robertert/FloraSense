#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_mac.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "flora_mqtt.h"
#include "mqtt_client.h"

#include "sensor_light.h"
#include "sensor_temp.h"
#include "sensor_soil.h"
#include "sensor_hall.h"
#include "sensor_ir.h"
#include "sensor_dock.h"
#include "motor_controller.h"
#include "wifi.h"
#include "config.h"

static const char *TAG = "flora-mqtt";

/* ---- BROKER ---- */
#define MQTT_BROKER_URI "mqtt://172.20.10.2:1883"

/* ---- DYNAMICZNE ID ---- */
static char user_id[32] = "default_user";   // ustawiane przez MQTT + NVS
static char device_id[32] = {0};            // generowane z MAC

#define PUB_INTERVAL_MS 10000

static esp_mqtt_client_handle_t client = NULL;
static bool mqtt_connected = false;

/* ---- KONFIGURACJA CZUJNIKÓW ---- */
// Konfiguracja czujnika światła 1 (port I2C 0)
static sensor_light_config_t light_config_1 = {
    .i2c_port = I2C_NUM_0,
    .sda_pin = GPIO_NUM_21,
    .scl_pin = GPIO_NUM_22,
    .i2c_freq_hz = 50000,
    .i2c_address = 0x10
};

// Konfiguracja czujnika światła 2 (port I2C 1)
static sensor_light_config_t light_config_2 = {
    .i2c_port = I2C_NUM_1,
    .sda_pin = GPIO_NUM_32,
    .scl_pin = GPIO_NUM_33,
    .i2c_freq_hz = 50000,
    .i2c_address = 0x10
};

// Piny dla czujników IR
#define IR_SENSOR_PIN_1 GPIO_NUM_25
#define IR_SENSOR_PIN_2 GPIO_NUM_26

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
    if (client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized, skipping publish: %s", topic_suffix);
        return;
    }

    if (!mqtt_connected) {
        ESP_LOGD(TAG, "MQTT not connected, skipping publish: %s", topic_suffix);
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic),
             "florasense/%s/%s/%s",
             user_id, device_id, topic_suffix);

    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish to %s (msg_id=%d)", topic, msg_id);
    } else {
        ESP_LOGI(TAG, "Published to %s | msg_id=%d | %s", topic, msg_id, payload);
    }
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
            mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT connected to broker %s!", MQTT_BROKER_URI);

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
                
                if (!motor_controller_is_initialized()) {
                    ESP_LOGW(TAG, "Sterownik silników nie jest zainicjalizowany");
                    break;
                }
                
                // Parsuj komendę JSON lub prosty tekst
                char cmd_data[128] = {0};
                memcpy(cmd_data, event->data, event->data_len < sizeof(cmd_data) - 1 ? event->data_len : sizeof(cmd_data) - 1);
                
                // Sprawdź czy to komenda z odległością (JSON)
                if (strstr(cmd_data, "distance") || strstr(cmd_data, "\"distance\"")) {
                    // Parsuj JSON: {"direction":"forward","distance":20,"speed":128}
                    float distance = 0.0f;
                    int16_t speed = 128; // Domyślna prędkość
                    char direction[32] = {0};
                    
                    // Prosty parser JSON (można użyć cJSON jeśli dostępny)
                    char *dir_ptr = strstr(cmd_data, "\"direction\"");
                    if (dir_ptr) {
                        char *dir_start = strchr(dir_ptr, ':');
                        if (dir_start) {
                            dir_start++; // Pomiń ':'
                            while (*dir_start == ' ' || *dir_start == '"') dir_start++;
                            sscanf(dir_start, "%31[^\",}]", direction);
                        }
                    }
                    
                    char *dist_ptr = strstr(cmd_data, "\"distance\"");
                    if (dist_ptr) {
                        char *dist_start = strchr(dist_ptr, ':');
                        if (dist_start) {
                            dist_start++; // Pomiń ':'
                            sscanf(dist_start, "%f", &distance);
                        }
                    }
                    
                    char *speed_ptr = strstr(cmd_data, "\"speed\"");
                    if (speed_ptr) {
                        int speed_val = 128;
                        char *speed_start = strchr(speed_ptr, ':');
                        if (speed_start) {
                            speed_start++; // Pomiń ':'
                            sscanf(speed_start, "%d", &speed_val);
                            speed = (int16_t)speed_val;
                        }
                    }
                    
                    if (distance > 0 && strlen(direction) > 0) {
                        ESP_LOGI(TAG, "Komenda z odległością: direction=%s, distance=%.2f cm, speed=%d", 
                                 direction, distance, speed);
                        motor_controller_move_distance(direction, distance, speed);
                    } else {
                        ESP_LOGW(TAG, "Nieprawidłowa komenda z odległością: %s", cmd_data);
                    }
                }
                // Proste komendy bez odległości (ciągłe działanie)
                else if (strstr(cmd_data, "forward") || strstr(cmd_data, "przód")) {
                    motor_controller_set_speeds(128, 128);
                    ESP_LOGI(TAG, "Silniki: Przód (ciągłe)");
                } else if (strstr(cmd_data, "backward") || strstr(cmd_data, "tył") || strstr(cmd_data, "wstecz")) {
                    motor_controller_set_speeds(-128, -128);
                    ESP_LOGI(TAG, "Silniki: Wstecz (ciągłe)");
                } else if (strstr(cmd_data, "left") || strstr(cmd_data, "lewo")) {
                    motor_controller_set_speeds(-128, 128);
                    ESP_LOGI(TAG, "Silniki: W lewo (ciągłe)");
                } else if (strstr(cmd_data, "right") || strstr(cmd_data, "prawo")) {
                    motor_controller_set_speeds(128, -128);
                    ESP_LOGI(TAG, "Silniki: W prawo (ciągłe)");
                } else if (strstr(cmd_data, "stop") || strstr(cmd_data, "stop")) {
                    motor_controller_stop();
                    ESP_LOGI(TAG, "Silniki: Stop");
                } else {
                    ESP_LOGW(TAG, "Nieznana komenda: %s", cmd_data);
                }
            }
        }
        break;

        case MQTT_EVENT_ERROR:
        {
            mqtt_connected = false;
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    ESP_LOGE(TAG, "Transport error: %s (errno: %d)", 
                             strerror(event->error_handle->esp_transport_sock_errno),
                             event->error_handle->esp_transport_sock_errno);
                    ESP_LOGE(TAG, "Cannot connect to broker %s - check if broker is running and accessible", MQTT_BROKER_URI);
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    ESP_LOGE(TAG, "Connection refused by broker");
                } else {
                    ESP_LOGE(TAG, "Error type: %d", event->error_handle->error_type);
                }
            }
        }
        break;

        case MQTT_EVENT_DISCONNECTED:
        {
            mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            // Automatyczne ponowne połączenie po 5 sekundach
            vTaskDelay(pdMS_TO_TICKS(5000));
            if (client != NULL) {
                ESP_LOGI(TAG, "Attempting to reconnect to MQTT broker...");
                esp_mqtt_client_reconnect(client);
            }
        }
        break;

        case MQTT_EVENT_BEFORE_CONNECT:
        {
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT - connecting to %s", MQTT_BROKER_URI);
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

    ESP_LOGI(TAG, "Starting MQTT client, broker: %s", MQTT_BROKER_URI);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .network.timeout_ms = 5000,   // 5 sekund timeout
        .network.reconnect_timeout_ms = 5000,  // 5 sekund przed ponownym połączeniem
        .session.keepalive = 60,       // Keepalive 60 sekund
        .session.disable_clean_session = false,  // Clean session
    };

    client = esp_mqtt_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    esp_mqtt_client_register_event(
        client, ESP_EVENT_ANY_ID,
        mqtt_event_handler_cb, NULL);

    esp_err_t ret = esp_mqtt_client_start(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "MQTT client started, connecting to broker...");
    }
}

/* -------------------------------------------------------
   Inicjalizacja czujników
--------------------------------------------------------*/
static void mqtt_init_sensors(void)
{
    ESP_LOGI(TAG, "Initializing sensors for MQTT...");
    
    // Inicjalizacja czujnika temperatury BME280
    esp_err_t ret = sensor_temp_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize temperature sensor: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Temperature sensor initialized");
    }
    
    // Inicjalizacja czujnika gleby
    ret = sensor_soil_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize soil sensor: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Soil sensor initialized");
    }
    
    // Inicjalizacja czujnika światła 1
    ret = sensor_light_init(&light_config_1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize light sensor 1: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Light sensor 1 initialized (Port %d, SDA=%d, SCL=%d)", 
                 light_config_1.i2c_port, light_config_1.sda_pin, light_config_1.scl_pin);
    }
    
    // Inicjalizacja czujnika światła 2
    ret = sensor_light_init(&light_config_2);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize light sensor 2: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Light sensor 2 initialized (Port %d, SDA=%d, SCL=%d)", 
                 light_config_2.i2c_port, light_config_2.sda_pin, light_config_2.scl_pin);
    }
    
    // Inicjalizacja czujnika IR 1
    ret = sensor_ir_init(IR_SENSOR_PIN_1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize IR sensor 1 (GPIO %d): %s", IR_SENSOR_PIN_1, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "IR sensor 1 initialized (GPIO %d)", IR_SENSOR_PIN_1);
    }
    
    // Inicjalizacja czujnika IR 2
    ret = sensor_ir_init(IR_SENSOR_PIN_2);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize IR sensor 2 (GPIO %d): %s", IR_SENSOR_PIN_2, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "IR sensor 2 initialized (GPIO %d)", IR_SENSOR_PIN_2);
    }
    
    // Inicjalizacja sterownika silników
    ret = motor_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize motor controller: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Motor controller initialized");
    }
    
    ESP_LOGI(TAG, "All sensors initialized");
    
    // Poczekaj chwilę na stabilizację czujników
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/* -------------------------------------------------------
   Task publikujący dane
--------------------------------------------------------*/
void mqtt_publish_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT publish task started");
    
    // Inicjalizacja wszystkich czujników
    mqtt_init_sensors();
    
    ESP_LOGI(TAG, "Starting to publish sensor data...");
    
    while (1)
    {
        char payload[128];

        /* --- ŚWIATŁO 1 --- */
        if (sensor_light_is_initialized(&light_config_1)) {
            sensor_light_reading_t light;
            if (sensor_light_read(&light_config_1, &light) == ESP_OK) {
                snprintf(payload, sizeof(payload),
                         "{\"value\": %.2f, \"unit\": \"lux\", \"sensor\": 1}", light.lux);
                publish_sensor("sensor/light", payload);
            }
        }

        /* --- ŚWIATŁO 2 --- */
        if (sensor_light_is_initialized(&light_config_2)) {
            sensor_light_reading_t light;
            if (sensor_light_read(&light_config_2, &light) == ESP_OK) {
                snprintf(payload, sizeof(payload),
                         "{\"value\": %.2f, \"unit\": \"lux\", \"sensor\": 2}", light.lux);
                publish_sensor("sensor/light", payload);
            }
        }

        /* --- TEMPERATURA + WILGOTNOŚĆ (2x) --- */
        sensor_temp_reading_t temp;
        if (sensor_temp_read(&temp) == ESP_OK) {
            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"C\", \"sensor\": 1}", temp.temperature_c);
            publish_sensor("sensor/temp", payload);

            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"%%\", \"sensor\": 1}", temp.humidity_percent);
            publish_sensor("sensor/humidity", payload);
                        
        }

        /* --- GLEBA (2x) --- */
        sensor_soil_reading_t soil;
        if (sensor_soil_read(&soil) == ESP_OK) {
            snprintf(payload, sizeof(payload),
                     "{\"value\": %.2f, \"unit\": \"%%\", \"mv\": %d, \"sensor\": 1}",
                     soil.moisture_percent, soil.millivolts);
            publish_sensor("sensor/soil", payload);
        
        }

        /* --- IR 1 --- */
        if (sensor_ir_is_initialized(IR_SENSOR_PIN_1)) {
            int ir_state = sensor_ir_read(IR_SENSOR_PIN_1);
            if (ir_state >= 0) {
                snprintf(payload, sizeof(payload),
                         "{\"obstacle\": %s, \"sensor\": 1}",
                         ir_state ? "true" : "false");
                publish_sensor("sensor/ir", payload);
            }
        }

        /* --- IR 2 --- */
        if (sensor_ir_is_initialized(IR_SENSOR_PIN_2)) {
            int ir_state = sensor_ir_read(IR_SENSOR_PIN_2);
            if (ir_state >= 0) {
                snprintf(payload, sizeof(payload),
                         "{\"obstacle\": %s, \"sensor\": 2}",
                         ir_state ? "true" : "false");
                publish_sensor("sensor/ir", payload);
            }
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

        /* --- BATERIA (mock) --- */
        int batt = 75;
        snprintf(payload, sizeof(payload),
                 "{\"value\": %d, \"unit\": \"%%\"}", batt);
        publish_sensor("state/battery", payload);

        vTaskDelay(pdMS_TO_TICKS(PUB_INTERVAL_MS));
    }
}
