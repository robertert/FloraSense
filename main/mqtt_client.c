#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_mac.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "flora_mqtt.h"
#include "mqtt_client.h"

#include "sensor_light.h"
#include "sensor_temp.h"
#include "sensor_soil.h"
#include "sensor_hall.h"
#include "sensor_ir.h"
#include "sensor_dock.h"
#include "sensor_battery.h"
#include "motor_controller.h"
#include "wifi.h"
#include "config.h"
#include "cJSON.h"

static const char *TAG = "flora-mqtt";

/* ---- BROKER ---- */
#define MQTT_BROKER_URI "mqtt://172.20.10.2:1883"

/* ---- TIMESTAMP HELPER ---- */
static void get_timestamp_str(char *buf, size_t buf_size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);
    
    snprintf(buf, buf_size, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld]",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec,
             tv.tv_usec / 1000);
}

/* ---- DYNAMICZNE ID ---- */
static char user_id[32] = "default_user";   // ustawiane przez MQTT + NVS
static char device_id[32] = {0};            // generowane z MAC

/* ---- KONFIGURACJA ---- */
#define DEFAULT_MEASUREMENT_INTERVAL_MS 10000
static uint32_t measurement_interval_ms = DEFAULT_MEASUREMENT_INTERVAL_MS;

typedef struct {
    float temp_min;
    float temp_max;
    float soil_moisture_min;
    float soil_moisture_max;
    float battery_min;  // Próg niskiego poziomu baterii
    bool valid;  // czy konfiguracja jest ustawiona
} alarm_config_t;

static alarm_config_t alarm_config = {
    .temp_min = 0.0f,
    .temp_max = 0.0f,
    .soil_moisture_min = 0.0f,
    .soil_moisture_max = 0.0f,
    .battery_min = 0.0f,
    .valid = false
};

static bool light_movement_enabled = false;
static float light_threshold = 10.0f;  // Domyślny próg światła (lux)
static float soil_humidity_threshold = 50.0f;  // Domyślny próg wilgotności gleby (%)
static bool water_enabled = false;  // Domyślnie wyłączone

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
   Gettery dla konfiguracji czujników (dla innych modułów)
--------------------------------------------------------*/
sensor_light_config_t* mqtt_get_light_config_1(void)
{
    return &light_config_1;
}

sensor_light_config_t* mqtt_get_light_config_2(void)
{
    return &light_config_2;
}

gpio_num_t mqtt_get_ir_sensor_pin_1(void)
{
    return IR_SENSOR_PIN_1;
}

gpio_num_t mqtt_get_ir_sensor_pin_2(void)
{
    return IR_SENSOR_PIN_2;
}

/* -------------------------------------------------------
   Gettery dla konfiguracji (dla innych modułów)
--------------------------------------------------------*/
bool mqtt_get_light_movement_enabled(void)
{
    return light_movement_enabled;
}

float mqtt_get_light_threshold(void)
{
    return light_threshold;
}

float mqtt_get_soil_humidity_threshold(void)
{
    return soil_humidity_threshold;
}

bool mqtt_get_water_enabled(void)
{
    return water_enabled;
}

/* -------------------------------------------------------
   NVS: zapis i odczyt USER_ID
--------------------------------------------------------*/
static void load_user_id_from_nvs()
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(user_id);
        if (nvs_get_str(handle, "user_id", user_id, &size) == ESP_OK) {
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s Loaded USER_ID from NVS: %s", timestamp, user_id);
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
        char timestamp[64];
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "%s Saved USER_ID to NVS: %s", timestamp, new_id);
    }
}

/* -------------------------------------------------------
   NVS: zapis i odczyt konfiguracji alarmów
--------------------------------------------------------*/
static void load_alarm_config_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(alarm_config);
        if (nvs_get_blob(handle, "alarm_config", &alarm_config, &size) == ESP_OK) {
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s Loaded alarm config from NVS: temp_min=%.1f, temp_max=%.1f, soil_min=%.1f, soil_max=%.1f, battery_min=%.1f",
                     timestamp, alarm_config.temp_min, alarm_config.temp_max,
                     alarm_config.soil_moisture_min, alarm_config.soil_moisture_max, alarm_config.battery_min);
        }
        nvs_close(handle);
    }
}

static void save_alarm_config_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "alarm_config", &alarm_config, sizeof(alarm_config));
        nvs_commit(handle);
        nvs_close(handle);
        char timestamp[64];
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "%s Saved alarm config to NVS: temp_min=%.1f, temp_max=%.1f, soil_min=%.1f, soil_max=%.1f, battery_min=%.1f",
                 timestamp, alarm_config.temp_min, alarm_config.temp_max,
                 alarm_config.soil_moisture_min, alarm_config.soil_moisture_max, alarm_config.battery_min);
    }
}

/* -------------------------------------------------------
   NVS: zapis i odczyt measurement_interval_ms
--------------------------------------------------------*/
static void load_measurement_interval_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        uint32_t interval = DEFAULT_MEASUREMENT_INTERVAL_MS;
        if (nvs_get_u32(handle, "measurement_interval_ms", &interval) == ESP_OK) {
            measurement_interval_ms = interval;
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s Loaded measurement_interval_ms from NVS: %lu ms", timestamp, measurement_interval_ms);
        }
        nvs_close(handle);
    }
}

static void save_measurement_interval_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, "measurement_interval_ms", measurement_interval_ms);
        nvs_commit(handle);
        nvs_close(handle);
        char timestamp[64];
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "%s Saved measurement_interval_ms to NVS: %lu ms", timestamp, measurement_interval_ms);
    }
}

/* -------------------------------------------------------
   NVS: zapis i odczyt light_movement_enabled
--------------------------------------------------------*/
static void load_light_movement_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t enabled = 0;
        if (nvs_get_u8(handle, "light_movement_enabled", &enabled) == ESP_OK) {
            light_movement_enabled = (enabled != 0);
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s Loaded light_movement_enabled from NVS: %s", timestamp, light_movement_enabled ? "true" : "false");
        }
        nvs_close(handle);
    }
}

static void save_light_movement_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "light_movement_enabled", light_movement_enabled ? 1 : 0);
        nvs_commit(handle);
        nvs_close(handle);
        char timestamp[64];
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "%s Saved light_movement_enabled to NVS: %s", timestamp, light_movement_enabled ? "true" : "false");
    }
}

/* -------------------------------------------------------
   NVS: zapis i odczyt konfiguracji urządzenia (rozszerzone)
--------------------------------------------------------*/
static void load_device_config_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        // light_threshold
        float threshold = 10.0f;
        size_t size = sizeof(float);
        if (nvs_get_blob(handle, "light_threshold", &threshold, &size) == ESP_OK) {
            light_threshold = threshold;
        }
        
        // soil_humidity_threshold
        threshold = 50.0f;
        size = sizeof(float);
        if (nvs_get_blob(handle, "soil_humidity_threshold", &threshold, &size) == ESP_OK) {
            soil_humidity_threshold = threshold;
        }
        
        // water_enabled
        uint8_t enabled = 0;
        if (nvs_get_u8(handle, "water_enabled", &enabled) == ESP_OK) {
            water_enabled = (enabled != 0);
        }
        
        nvs_close(handle);
        char timestamp[64];
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "%s Loaded device config from NVS: light_threshold=%.1f, soil_humidity_threshold=%.1f, water_enabled=%s",
                 timestamp, light_threshold, soil_humidity_threshold, water_enabled ? "true" : "false");
    }
}

static void save_device_config_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        // light_threshold
        nvs_set_blob(handle, "light_threshold", &light_threshold, sizeof(float));
        
        // soil_humidity_threshold
        nvs_set_blob(handle, "soil_humidity_threshold", &soil_humidity_threshold, sizeof(float));
        
        // water_enabled
        nvs_set_u8(handle, "water_enabled", water_enabled ? 1 : 0);
        
        nvs_commit(handle);
        nvs_close(handle);
        char timestamp[64];
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "%s Saved device config to NVS: light_threshold=%.1f, soil_humidity_threshold=%.1f, water_enabled=%s",
                 timestamp, light_threshold, soil_humidity_threshold, water_enabled ? "true" : "false");
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
    char timestamp[64];
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (msg_id < 0) {
        ESP_LOGW(TAG, "%s Failed to publish to %s (msg_id=%d)", timestamp, topic, msg_id);
    } else {
        ESP_LOGI(TAG, "%s Published to %s | msg_id=%d | %s", timestamp, topic, msg_id, payload);
    }
}

/* -------------------------------------------------------
   Subskrypcja tematów komend z aktualnym user_id
--------------------------------------------------------*/
static void subscribe_to_commands(void)
{
    if (!mqtt_connected || client == NULL) {
        return;
    }

    char water_cmd[128];
    char move_cmd[128];
    char user_cfg[128];
    char alarm_cfg[128];
    char measurement_cfg[128];
    char device_cfg[128];

    snprintf(water_cmd, sizeof(water_cmd),
             "florasense/%s/%s/cmd/water",
             user_id, device_id);

    snprintf(move_cmd, sizeof(move_cmd),
             "florasense/%s/%s/cmd/move",
             user_id, device_id);

    snprintf(user_cfg, sizeof(user_cfg),
             "florasense/%s/config/user",
             device_id);

    snprintf(alarm_cfg, sizeof(alarm_cfg),
             "florasense/%s/config/alarm",
             device_id);

    snprintf(measurement_cfg, sizeof(measurement_cfg),
             "florasense/%s/config/measurement",
             device_id);

    snprintf(device_cfg, sizeof(device_cfg),
             "florasense/%s/config/device",
             device_id);

    esp_mqtt_client_subscribe(client, water_cmd, 1);
    esp_mqtt_client_subscribe(client, move_cmd, 1);
    esp_mqtt_client_subscribe(client, user_cfg, 1);
    esp_mqtt_client_subscribe(client, alarm_cfg, 1);
    esp_mqtt_client_subscribe(client, measurement_cfg, 1);
    esp_mqtt_client_subscribe(client, device_cfg, 1);

    char timestamp[64];
    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s Subscribed to: %s, %s, %s, %s, %s, %s",
             timestamp, water_cmd, move_cmd, user_cfg, alarm_cfg, measurement_cfg, device_cfg);
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
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s MQTT connected to broker %s!", timestamp, MQTT_BROKER_URI);

            /* --- SUBSKRYPCJA KOMEND --- */
            subscribe_to_commands();
        }
        break;

        case MQTT_EVENT_DATA:
        {
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s [CMD] Topic: %.*s | Data: %.*s",
                     timestamp, event->topic_len, event->topic,
                     event->data_len, event->data);

            /* --- USTAWIANIE USER_ID PRZEZ MQTT --- */
            if (strstr(event->topic, "/config/user"))
            {
                char new_user[32] = {0};
                memcpy(new_user, event->data, event->data_len);

                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s Received USER_ID config: %s", timestamp, new_user);

                save_user_id_to_nvs(new_user);
                strncpy(user_id, new_user, sizeof(user_id));
                
                /* --- RE-SUBSKRYPCJA TEMATÓW Z NOWYM USER_ID --- */
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s Re-subscribing to commands with new USER_ID: %s", timestamp, user_id);
                subscribe_to_commands();
            }

            /* --- KONFIGURACJA ALARMÓW --- */
            else if (strstr(event->topic, "/config/alarm"))
            {
                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s Received alarm config: %.*s", timestamp, event->data_len, event->data);

                // Parsuj JSON
                char json_data[256] = {0};
                memcpy(json_data, event->data, event->data_len < sizeof(json_data) - 1 ? event->data_len : sizeof(json_data) - 1);

                cJSON *json = cJSON_Parse(json_data);
                if (json == NULL) {
                    const char *error_ptr = cJSON_GetErrorPtr();
                    if (error_ptr != NULL) {
                        ESP_LOGE(TAG, "%s Error parsing alarm config JSON: %s", timestamp, error_ptr);
                    }
                } else {
                    bool updated = false;

                    cJSON *temp_min = cJSON_GetObjectItemCaseSensitive(json, "temp_min");
                    if (cJSON_IsNumber(temp_min)) {
                        alarm_config.temp_min = (float)temp_min->valuedouble;
                        updated = true;
                    }

                    cJSON *temp_max = cJSON_GetObjectItemCaseSensitive(json, "temp_max");
                    if (cJSON_IsNumber(temp_max)) {
                        alarm_config.temp_max = (float)temp_max->valuedouble;
                        updated = true;
                    }

                    cJSON *soil_moisture_min = cJSON_GetObjectItemCaseSensitive(json, "soil_moisture_min");
                    if (cJSON_IsNumber(soil_moisture_min)) {
                        alarm_config.soil_moisture_min = (float)soil_moisture_min->valuedouble;
                        updated = true;
                    }

                    cJSON *soil_moisture_max = cJSON_GetObjectItemCaseSensitive(json, "soil_moisture_max");
                    if (cJSON_IsNumber(soil_moisture_max)) {
                        alarm_config.soil_moisture_max = (float)soil_moisture_max->valuedouble;
                        updated = true;
                    }

                    cJSON *battery_min = cJSON_GetObjectItemCaseSensitive(json, "battery_min");
                    if (cJSON_IsNumber(battery_min)) {
                        alarm_config.battery_min = (float)battery_min->valuedouble;
                        updated = true;
                    }

                    if (updated) {
                        alarm_config.valid = true;
                        save_alarm_config_to_nvs();
                        ESP_LOGI(TAG, "%s Alarm config updated: temp_min=%.1f, temp_max=%.1f, soil_min=%.1f, soil_max=%.1f, battery_min=%.1f",
                                 timestamp, alarm_config.temp_min, alarm_config.temp_max,
                                 alarm_config.soil_moisture_min, alarm_config.soil_moisture_max, alarm_config.battery_min);
                    }

                    cJSON_Delete(json);
                }
            }

            /* --- KONFIGURACJA POMIARÓW --- */
            else if (strstr(event->topic, "/config/measurement"))
            {
                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s Received measurement config: %.*s", timestamp, event->data_len, event->data);

                // Parsuj JSON
                char json_data[256] = {0};
                memcpy(json_data, event->data, event->data_len < sizeof(json_data) - 1 ? event->data_len : sizeof(json_data) - 1);

                cJSON *json = cJSON_Parse(json_data);
                if (json == NULL) {
                    const char *error_ptr = cJSON_GetErrorPtr();
                    if (error_ptr != NULL) {
                        ESP_LOGE(TAG, "%s Error parsing measurement config JSON: %s", timestamp, error_ptr);
                    }
                } else {
                    cJSON *interval = cJSON_GetObjectItemCaseSensitive(json, "measurement_interval_ms");
                    if (cJSON_IsNumber(interval)) {
                        uint32_t new_interval = (uint32_t)interval->valueint;
                        if (new_interval >= 1000 && new_interval <= 600000) {  // Walidacja: 1s - 10min
                            measurement_interval_ms = new_interval;
                            save_measurement_interval_to_nvs();
                            ESP_LOGI(TAG, "%s Measurement interval updated: %lu ms", timestamp, measurement_interval_ms);
                        } else {
                            ESP_LOGW(TAG, "%s Invalid measurement_interval_ms: %lu (must be 1000-600000)", timestamp, new_interval);
                        }
                    } else {
                        ESP_LOGE(TAG, "%s measurement_interval_ms not found or not a number", timestamp);
                    }

                    cJSON_Delete(json);
                }
            }

            /* --- KONFIGURACJA URZĄDZENIA --- */
            else if (strstr(event->topic, "/config/device"))
            {
                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s Received device config: %.*s", timestamp, event->data_len, event->data);

                // Parsuj JSON
                char json_data[256] = {0};
                memcpy(json_data, event->data, event->data_len < sizeof(json_data) - 1 ? event->data_len : sizeof(json_data) - 1);

                cJSON *json = cJSON_Parse(json_data);
                if (json == NULL) {
                    const char *error_ptr = cJSON_GetErrorPtr();
                    if (error_ptr != NULL) {
                        ESP_LOGE(TAG, "%s Error parsing device config JSON: %s", timestamp, error_ptr);
                    }
                } else {
                    bool config_updated = false;

                    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(json, "light_movement_enabled");
                    if (cJSON_IsBool(enabled)) {
                        light_movement_enabled = cJSON_IsTrue(enabled);
                        save_light_movement_to_nvs();
                        ESP_LOGI(TAG, "%s Light movement enabled updated: %s", timestamp, light_movement_enabled ? "true" : "false");
                        config_updated = true;
                    }

                    cJSON *light_thresh = cJSON_GetObjectItemCaseSensitive(json, "light_threshold");
                    if (cJSON_IsNumber(light_thresh)) {
                        light_threshold = (float)light_thresh->valuedouble;
                        config_updated = true;
                        ESP_LOGI(TAG, "%s Light threshold updated: %.1f lux", timestamp, light_threshold);
                    }

                    cJSON *soil_thresh = cJSON_GetObjectItemCaseSensitive(json, "soil_humidity_threshold");
                    if (cJSON_IsNumber(soil_thresh)) {
                        soil_humidity_threshold = (float)soil_thresh->valuedouble;
                        config_updated = true;
                        ESP_LOGI(TAG, "%s Soil humidity threshold updated: %.1f%%", timestamp, soil_humidity_threshold);
                    }

                    cJSON *water_en = cJSON_GetObjectItemCaseSensitive(json, "water_enabled");
                    if (cJSON_IsBool(water_en)) {
                        water_enabled = cJSON_IsTrue(water_en);
                        config_updated = true;
                        ESP_LOGI(TAG, "%s Water enabled updated: %s", timestamp, water_enabled ? "true" : "false");
                    }

                    if (config_updated) {
                        save_device_config_to_nvs();
                    }

                    cJSON_Delete(json);
                }
            }

            /* --- KOMENDA WATER --- */
            else if (strstr(event->topic, "/cmd/water"))
            {
                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s WATER_CMD received: %.*s",
                         timestamp, event->data_len, event->data);
            }

            /* --- KOMENDA MOVE --- */
            else if (strstr(event->topic, "/cmd/move"))
            {
                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s MOVE_CMD received: %.*s",
                         timestamp, event->data_len, event->data);
                
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
                    
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    if (distance > 0 && strlen(direction) > 0) {
                        ESP_LOGI(TAG, "%s Komenda z odległością: direction=%s, distance=%.2f cm, speed=%d", 
                                 timestamp, direction, distance, speed);
                        motor_controller_move_distance(direction, distance, speed);
                    } else {
                        ESP_LOGW(TAG, "%s Nieprawidłowa komenda z odległością: %s", timestamp, cmd_data);
                    }
                }
                // Proste komendy bez odległości (ciągłe działanie)
                else if (strstr(cmd_data, "forward") || strstr(cmd_data, "przód")) {
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    motor_controller_set_speeds(128, 128);
                    ESP_LOGI(TAG, "%s Silniki: Przód (ciągłe)", timestamp);
                } else if (strstr(cmd_data, "backward") || strstr(cmd_data, "tył") || strstr(cmd_data, "wstecz")) {
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    motor_controller_set_speeds(-128, -128);
                    ESP_LOGI(TAG, "%s Silniki: Wstecz (ciągłe)", timestamp);
                } else if (strstr(cmd_data, "left") || strstr(cmd_data, "lewo")) {
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    motor_controller_set_speeds(-128, 128);
                    ESP_LOGI(TAG, "%s Silniki: W lewo (ciągłe)", timestamp);
                } else if (strstr(cmd_data, "right") || strstr(cmd_data, "prawo")) {
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    motor_controller_set_speeds(128, -128);
                    ESP_LOGI(TAG, "%s Silniki: W prawo (ciągłe)", timestamp);
                } else if (strstr(cmd_data, "stop") || strstr(cmd_data, "stop")) {
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    motor_controller_stop();
                    ESP_LOGI(TAG, "%s Silniki: Stop", timestamp);
                } else {
                    char timestamp[64];
                    get_timestamp_str(timestamp, sizeof(timestamp));
                    ESP_LOGW(TAG, "%s Nieznana komenda: %s", timestamp, cmd_data);
                }
            }
        }
        break;

        case MQTT_EVENT_ERROR:
        {
            mqtt_connected = false;
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGE(TAG, "%s MQTT_EVENT_ERROR", timestamp);
            if (event->error_handle) {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    ESP_LOGE(TAG, "%s Transport error: %s (errno: %d)", 
                             timestamp, strerror(event->error_handle->esp_transport_sock_errno),
                             event->error_handle->esp_transport_sock_errno);
                    ESP_LOGE(TAG, "%s Cannot connect to broker %s - check if broker is running and accessible", 
                             timestamp, MQTT_BROKER_URI);
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    ESP_LOGE(TAG, "%s Connection refused by broker", timestamp);
                } else {
                    ESP_LOGE(TAG, "%s Error type: %d", timestamp, event->error_handle->error_type);
                }
            }
        }
        break;

        case MQTT_EVENT_DISCONNECTED:
        {
            mqtt_connected = false;
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGW(TAG, "%s MQTT_EVENT_DISCONNECTED", timestamp);
            // Automatyczne ponowne połączenie po 5 sekundach
            vTaskDelay(pdMS_TO_TICKS(5000));
            if (client != NULL) {
                char timestamp[64];
                get_timestamp_str(timestamp, sizeof(timestamp));
                ESP_LOGI(TAG, "%s Attempting to reconnect to MQTT broker...", timestamp);
                esp_mqtt_client_reconnect(client);
            }
        }
        break;

        case MQTT_EVENT_BEFORE_CONNECT:
        {
            char timestamp[64];
            get_timestamp_str(timestamp, sizeof(timestamp));
            ESP_LOGI(TAG, "%s MQTT_EVENT_BEFORE_CONNECT - connecting to %s", timestamp, MQTT_BROKER_URI);
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

    char timestamp[64];
    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s Generated DEVICE_ID: %s", timestamp, device_id);

    /* --- ŁADOWANIE USER_ID Z NVS --- */
    load_user_id_from_nvs();

    /* --- ŁADOWANIE KONFIGURACJI Z NVS --- */
    load_alarm_config_from_nvs();
    load_measurement_interval_from_nvs();
    load_light_movement_from_nvs();
    load_device_config_from_nvs();

    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s Starting MQTT client, broker: %s", timestamp, MQTT_BROKER_URI);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .network.timeout_ms = 5000,   // 5 sekund timeout
        .network.reconnect_timeout_ms = 5000,  // 5 sekund przed ponownym połączeniem
        .session.keepalive = 60,       // Keepalive 60 sekund
        .session.disable_clean_session = false,  // Clean session
    };

    client = esp_mqtt_client_init(&cfg);
    if (client == NULL) {
        get_timestamp_str(timestamp, sizeof(timestamp));
        ESP_LOGE(TAG, "%s Failed to initialize MQTT client", timestamp);
        return;
    }

    esp_mqtt_client_register_event(
        client, ESP_EVENT_ANY_ID,
        mqtt_event_handler_cb, NULL);

    esp_err_t ret = esp_mqtt_client_start(client);
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s Failed to start MQTT client: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s MQTT client started, connecting to broker...", timestamp);
    }
}

/* -------------------------------------------------------
   Inicjalizacja czujników
--------------------------------------------------------*/
static void mqtt_init_sensors(void)
{
    char timestamp[64];
    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s Initializing sensors for MQTT...", timestamp);
    
    // Inicjalizacja czujnika temperatury BME280
    esp_err_t ret = sensor_temp_init();
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize temperature sensor: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s Temperature sensor initialized", timestamp);
    }
    
    // Inicjalizacja czujnika gleby
    ret = sensor_soil_init();
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize soil sensor: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s Soil sensor initialized", timestamp);
    }
    
    // Inicjalizacja czujnika światła 1
    ret = sensor_light_init(&light_config_1);
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize light sensor 1: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s Light sensor 1 initialized (Port %d, SDA=%d, SCL=%d)", 
                 timestamp, light_config_1.i2c_port, light_config_1.sda_pin, light_config_1.scl_pin);
    }
    
    // Inicjalizacja czujnika światła 2
    ret = sensor_light_init(&light_config_2);
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize light sensor 2: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s Light sensor 2 initialized (Port %d, SDA=%d, SCL=%d)", 
                 timestamp, light_config_2.i2c_port, light_config_2.sda_pin, light_config_2.scl_pin);
    }
    
    // Inicjalizacja czujnika IR 1
    ret = sensor_ir_init(IR_SENSOR_PIN_1);
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize IR sensor 1 (GPIO %d): %s", timestamp, IR_SENSOR_PIN_1, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s IR sensor 1 initialized (GPIO %d)", timestamp, IR_SENSOR_PIN_1);
    }
    
    // Inicjalizacja czujnika IR 2
    ret = sensor_ir_init(IR_SENSOR_PIN_2);
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize IR sensor 2 (GPIO %d): %s", timestamp, IR_SENSOR_PIN_2, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s IR sensor 2 initialized (GPIO %d)", timestamp, IR_SENSOR_PIN_2);
    }
    
    // Inicjalizacja sterownika silników
    ret = motor_controller_init();
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize motor controller: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s Motor controller initialized", timestamp);
    }
    
    // Inicjalizacja modułu monitorowania baterii
    ret = sensor_battery_init();
    get_timestamp_str(timestamp, sizeof(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s Failed to initialize battery sensor: %s", timestamp, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s Battery sensor initialized", timestamp);
    }
    
    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s All sensors initialized", timestamp);
    
    // Poczekaj chwilę na stabilizację czujników
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/* -------------------------------------------------------
   Task publikujący dane
--------------------------------------------------------*/
void mqtt_publish_task(void *pvParameters)
{
    char timestamp[64];
    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s MQTT publish task started", timestamp);
    
    // Inicjalizacja wszystkich czujników
    mqtt_init_sensors();
    
    get_timestamp_str(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG, "%s Starting to publish sensor data...", timestamp);
    
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

        /* --- BATERIA --- */
        sensor_battery_reading_t battery = {0};
        bool battery_read_ok = false;
        if (sensor_battery_is_initialized()) {
            if (sensor_battery_read(&battery) == ESP_OK) {
                battery_read_ok = true;
                snprintf(payload, sizeof(payload),
                         "{\"value\": %.1f, \"unit\": \"%%\", \"voltage\": %d, \"voltage_unit\": \"mV\"}",
                         battery.battery_percent, battery.millivolts);
                publish_sensor("state/battery", payload);
                
                // Aktualizuj LED z dynamicznym progiem
                float battery_threshold = 0.0f;  // 0 = użyj domyślnego z config.h
                if (alarm_config.valid && alarm_config.battery_min > 0) {
                    battery_threshold = alarm_config.battery_min;
                }
                sensor_battery_update_led(battery.battery_percent, battery_threshold);
            } else {
                ESP_LOGW(TAG, "Błąd odczytu poziomu baterii");
            }
        } else {
            ESP_LOGW(TAG, "Moduł baterii nie jest zainicjalizowany");
        }

        /* --- SPRAWDZANIE ALARMÓW --- */
        if (alarm_config.valid) {
            bool alarm_triggered = false;
            char alarm_payload[256] = {0};
            int alarm_count = 0;

            // Sprawdź temperaturę
            if (temp.temperature_c > 0) {  // Tylko jeśli odczyt temperatury się powiódł
                if (alarm_config.temp_min > 0 && temp.temperature_c < alarm_config.temp_min) {
                    if (alarm_count > 0) snprintf(alarm_payload + strlen(alarm_payload), 
                        sizeof(alarm_payload) - strlen(alarm_payload), ",");
                    snprintf(alarm_payload + strlen(alarm_payload),
                        sizeof(alarm_payload) - strlen(alarm_payload),
                        "\"temp_low\":{\"value\":%.2f,\"threshold\":%.2f,\"unit\":\"C\"}",
                        temp.temperature_c, alarm_config.temp_min);
                    alarm_triggered = true;
                    alarm_count++;
                }
                if (alarm_config.temp_max > 0 && temp.temperature_c > alarm_config.temp_max) {
                    if (alarm_count > 0) snprintf(alarm_payload + strlen(alarm_payload), 
                        sizeof(alarm_payload) - strlen(alarm_payload), ",");
                    snprintf(alarm_payload + strlen(alarm_payload),
                        sizeof(alarm_payload) - strlen(alarm_payload),
                        "\"temp_high\":{\"value\":%.2f,\"threshold\":%.2f,\"unit\":\"C\"}",
                        temp.temperature_c, alarm_config.temp_max);
                    alarm_triggered = true;
                    alarm_count++;
                }
            }

            // Sprawdź wilgotność gleby
            if (soil.moisture_percent > 0) {  // Tylko jeśli odczyt gleby się powiódł
                if (alarm_config.soil_moisture_min > 0 && soil.moisture_percent < alarm_config.soil_moisture_min) {
                    if (alarm_count > 0) snprintf(alarm_payload + strlen(alarm_payload), 
                        sizeof(alarm_payload) - strlen(alarm_payload), ",");
                    snprintf(alarm_payload + strlen(alarm_payload),
                        sizeof(alarm_payload) - strlen(alarm_payload),
                        "\"soil_low\":{\"value\":%.2f,\"threshold\":%.2f,\"unit\":\"%%\"}",
                        soil.moisture_percent, alarm_config.soil_moisture_min);
                    alarm_triggered = true;
                    alarm_count++;
                }
                if (alarm_config.soil_moisture_max > 0 && soil.moisture_percent > alarm_config.soil_moisture_max) {
                    if (alarm_count > 0) snprintf(alarm_payload + strlen(alarm_payload), 
                        sizeof(alarm_payload) - strlen(alarm_payload), ",");
                    snprintf(alarm_payload + strlen(alarm_payload),
                        sizeof(alarm_payload) - strlen(alarm_payload),
                        "\"soil_high\":{\"value\":%.2f,\"threshold\":%.2f,\"unit\":\"%%\"}",
                        soil.moisture_percent, alarm_config.soil_moisture_max);
                    alarm_triggered = true;
                    alarm_count++;
                }
            }

            // Sprawdź baterię
            if (battery_read_ok && alarm_config.battery_min > 0) {
                if (battery.battery_percent < alarm_config.battery_min) {
                    if (alarm_count > 0) snprintf(alarm_payload + strlen(alarm_payload), 
                        sizeof(alarm_payload) - strlen(alarm_payload), ",");
                    snprintf(alarm_payload + strlen(alarm_payload),
                        sizeof(alarm_payload) - strlen(alarm_payload),
                        "\"battery_low\":{\"value\":%.1f,\"threshold\":%.1f,\"unit\":\"%%\"}",
                        battery.battery_percent, alarm_config.battery_min);
                    alarm_triggered = true;
                    alarm_count++;
                }
            }

            // Opublikuj alarm jeśli został wyzwolony
            if (alarm_triggered) {
                char full_alarm_payload[300];
                snprintf(full_alarm_payload, sizeof(full_alarm_payload), "{%s}", alarm_payload);
                publish_sensor("alarm", full_alarm_payload);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(measurement_interval_ms));
    }
}
