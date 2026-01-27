#include "ble_dock_client.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>
#include "sensor_soil.h"
#include "motor_controller.h"
#include "flora_mqtt.h"
#include "water_controller.h"

static const char *TAG = "ble_dock_client";

// Flora Dock Service UUID: 12345678-1234-5678-1234-56789abcdef0
static uint8_t service_uuid[16] = {
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

// Hall Sensor Characteristic UUID: ...def1
static uint8_t hall_char_uuid[16] = {
    0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

// Pump Control Characteristic UUID: ...def2
static uint8_t pump_char_uuid[16] = {
    0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

#define GATTC_APP_ID            0
#define TARGET_DEVICE_NAME      "FloraDock"
#define SCAN_DURATION           30

// Event bits
#define EVT_CONNECTED           BIT0
#define EVT_DISCONNECTED        BIT1
#define EVT_SERVICE_FOUND       BIT2
#define EVT_WRITE_COMPLETE      BIT3
#define EVT_READ_COMPLETE       BIT4
#define EVT_NOTIFY_ENABLED      BIT5

// Connection state
typedef enum {
    STATE_IDLE,
    STATE_SCANNING,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_DISCOVERING,
    STATE_READY,
} client_state_t;

static client_state_t client_state = STATE_IDLE;
static portMUX_TYPE client_lock = portMUX_INITIALIZER_UNLOCKED;
static EventGroupHandle_t event_group = NULL;
static SemaphoreHandle_t api_mutex = NULL;

// GATT handles
static esp_gatt_if_t gattc_if_global = ESP_GATT_IF_NONE;
static uint16_t conn_id = 0;
static uint16_t service_start_handle = 0;
static uint16_t service_end_handle = 0;
static uint16_t hall_char_handle = 0;
static uint16_t hall_cccd_handle = 0;
static uint16_t pump_char_handle = 0;

// Target device address
static esp_bd_addr_t target_bda = {0};
static esp_ble_addr_type_t target_addr_type = BLE_ADDR_TYPE_PUBLIC;
static bool target_found = false;

// Cached hall state
static volatile uint8_t cached_hall_state = 1;
static uint8_t read_hall_value = 1;

// Write result
static esp_gatt_status_t write_status = ESP_GATT_OK;

// Reconnection task
static TaskHandle_t reconnect_task_handle = NULL;
static volatile bool reconnect_enabled = true;

// Scan parameters
// Longer scan window for better reliability
static esp_ble_scan_params_t scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x80,  // 80ms
    .scan_window = 0x70,    // 70ms (nearly continuous scan)
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static void set_state(client_state_t new_state)
{
    taskENTER_CRITICAL(&client_lock);
    client_state = new_state;
    taskEXIT_CRITICAL(&client_lock);
}

static client_state_t get_state(void)
{
    taskENTER_CRITICAL(&client_lock);
    client_state_t state = client_state;
    taskEXIT_CRITICAL(&client_lock);
    return state;
}

static void start_scan(void)
{
    if (get_state() != STATE_IDLE) {
        return;
    }

    set_state(STATE_SCANNING);
    target_found = false;

    esp_err_t ret = esp_ble_gap_start_scanning(SCAN_DURATION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start scan failed: %s", esp_err_to_name(ret));
        set_state(STATE_IDLE);
    } else {
        ESP_LOGI(TAG, "Scanning for FloraDock...");
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan params set");
            start_scan();
        }
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Scan start failed: %d", param->scan_start_cmpl.status);
            set_state(STATE_IDLE);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            // Check if this is our target device by name
            uint8_t *adv_name = NULL;
            uint8_t adv_name_len = 0;
            adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                 ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            if (adv_name == NULL) {
                adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                     ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
            }

            if (adv_name != NULL && adv_name_len == strlen(TARGET_DEVICE_NAME) &&
                memcmp(adv_name, TARGET_DEVICE_NAME, adv_name_len) == 0) {

                ESP_LOGI(TAG, "Found FloraDock: " ESP_BD_ADDR_STR " (addr_type=%d)",
                         ESP_BD_ADDR_HEX(param->scan_rst.bda), param->scan_rst.ble_addr_type);

                memcpy(target_bda, param->scan_rst.bda, sizeof(esp_bd_addr_t));
                target_addr_type = param->scan_rst.ble_addr_type;
                target_found = true;

                // Stop scanning and connect
                esp_ble_gap_stop_scanning();
            }
        } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            ESP_LOGI(TAG, "Scan complete");
            if (target_found && get_state() == STATE_SCANNING) {
                set_state(STATE_CONNECTING);
                esp_ble_gattc_open(gattc_if_global, target_bda, target_addr_type, true);
            } else if (get_state() == STATE_SCANNING) {
                set_state(STATE_IDLE);
                ESP_LOGW(TAG, "FloraDock not found");
            }
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Scan stopped");
            if (target_found && get_state() == STATE_SCANNING) {
                set_state(STATE_CONNECTING);
                // Small delay before connection to let BT stack settle
                vTaskDelay(pdMS_TO_TICKS(100));
                ESP_LOGI(TAG, "Connecting to FloraDock...");
                esp_ble_gattc_open(gattc_if_global, target_bda, target_addr_type, true);
            }
        }
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Connection params updated");
        break;

    default:
        break;
    }
}

static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            gattc_if_global = gattc_if;
            ESP_LOGI(TAG, "GATTC app registered");

            // Set scan params
            esp_ble_gap_set_scan_params(&scan_params);
        }
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
            conn_id = param->open.conn_id;
            set_state(STATE_CONNECTED);
            ESP_LOGI(TAG, "Connected to FloraDock, conn_id=%d", conn_id);

            xEventGroupSetBits(event_group, EVT_CONNECTED);
            xEventGroupClearBits(event_group, EVT_DISCONNECTED);

            // Request MTU
            esp_ble_gattc_send_mtu_req(gattc_if, conn_id);
        } else {
            ESP_LOGE(TAG, "Connect failed: %d", param->open.status);
            set_state(STATE_IDLE);
        }
        break;

    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "MTU configured: %d", param->cfg_mtu.mtu);
        }

        // Start service discovery
        set_state(STATE_DISCOVERING);
        esp_bt_uuid_t srv_uuid = {
            .len = ESP_UUID_LEN_128,
        };
        memcpy(srv_uuid.uuid.uuid128, service_uuid, 16);
        esp_ble_gattc_search_service(gattc_if, conn_id, &srv_uuid);
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
            memcmp(param->search_res.srvc_id.uuid.uuid.uuid128, service_uuid, 16) == 0) {

            service_start_handle = param->search_res.start_handle;
            service_end_handle = param->search_res.end_handle;
            ESP_LOGI(TAG, "Flora Dock service found: handles %d-%d",
                     service_start_handle, service_end_handle);
        }
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (param->search_cmpl.status == ESP_GATT_OK && service_start_handle != 0) {
            ESP_LOGI(TAG, "Service discovery complete, getting characteristics");

            // Get all characteristics
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if, conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     service_start_handle,
                                                                     service_end_handle,
                                                                     0, &count);
            if (status == ESP_GATT_OK && count > 0) {
                esp_gattc_char_elem_t *char_result = malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (char_result) {
                    status = esp_ble_gattc_get_all_char(gattc_if, conn_id,
                                                        service_start_handle, service_end_handle,
                                                        char_result, &count, 0);
                    if (status == ESP_GATT_OK) {
                        for (int i = 0; i < count; i++) {
                            if (char_result[i].uuid.len == ESP_UUID_LEN_128) {
                                if (memcmp(char_result[i].uuid.uuid.uuid128, hall_char_uuid, 16) == 0) {
                                    hall_char_handle = char_result[i].char_handle;
                                    ESP_LOGI(TAG, "Hall char handle: %d", hall_char_handle);
                                } else if (memcmp(char_result[i].uuid.uuid.uuid128, pump_char_uuid, 16) == 0) {
                                    pump_char_handle = char_result[i].char_handle;
                                    ESP_LOGI(TAG, "Pump char handle: %d", pump_char_handle);
                                }
                            }
                        }
                    }
                    free(char_result);
                }
            }

            // Get CCCD for hall characteristic
            if (hall_char_handle != 0) {
                uint16_t descr_count = 0;
                status = esp_ble_gattc_get_attr_count(gattc_if, conn_id,
                                                       ESP_GATT_DB_DESCRIPTOR,
                                                       service_start_handle,
                                                       service_end_handle,
                                                       hall_char_handle, &descr_count);
                if (status == ESP_GATT_OK && descr_count > 0) {
                    esp_gattc_descr_elem_t *descr_result = malloc(sizeof(esp_gattc_descr_elem_t) * descr_count);
                    if (descr_result) {
                        status = esp_ble_gattc_get_all_descr(gattc_if, conn_id,
                                                             hall_char_handle,
                                                             descr_result, &descr_count, 0);
                        if (status == ESP_GATT_OK) {
                            for (int i = 0; i < descr_count; i++) {
                                if (descr_result[i].uuid.len == ESP_UUID_LEN_16 &&
                                    descr_result[i].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
                                    hall_cccd_handle = descr_result[i].handle;
                                    ESP_LOGI(TAG, "Hall CCCD handle: %d", hall_cccd_handle);
                                    break;
                                }
                            }
                        }
                        free(descr_result);
                    }
                }

                // Enable notifications
                if (hall_cccd_handle != 0) {
                    uint16_t notify_enable = 0x0001;
                    esp_ble_gattc_write_char_descr(gattc_if, conn_id, hall_cccd_handle,
                                                   sizeof(notify_enable), (uint8_t *)&notify_enable,
                                                   ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
                } else {
                    // No CCCD, mark as ready anyway
                    set_state(STATE_READY);
                    xEventGroupSetBits(event_group, EVT_SERVICE_FOUND);
                    ESP_LOGI(TAG, "BLE client ready (no notifications)");
                }
            }
        } else {
            ESP_LOGE(TAG, "Service not found");
            esp_ble_gattc_close(gattc_if, conn_id);
        }
        break;

    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status == ESP_GATT_OK && param->write.handle == hall_cccd_handle) {
            ESP_LOGI(TAG, "Notifications enabled");
            set_state(STATE_READY);
            xEventGroupSetBits(event_group, EVT_SERVICE_FOUND | EVT_NOTIFY_ENABLED);
            ESP_LOGI(TAG, "BLE client ready");
        }
        break;

    case ESP_GATTC_WRITE_CHAR_EVT:
        write_status = param->write.status;
        xEventGroupSetBits(event_group, EVT_WRITE_COMPLETE);
        if (param->write.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Write complete");
        } else {
            ESP_LOGW(TAG, "Write failed: %d", param->write.status);
        }
        break;

    case ESP_GATTC_READ_CHAR_EVT:
        if (param->read.status == ESP_GATT_OK && param->read.handle == hall_char_handle) {
            if (param->read.value_len >= 1) {
                read_hall_value = param->read.value[0];
                cached_hall_state = read_hall_value;
                ESP_LOGI(TAG, "Hall state read: %d", read_hall_value);
            }
        }
        xEventGroupSetBits(event_group, EVT_READ_COMPLETE);
        break;

    case ESP_GATTC_NOTIFY_EVT:
        if (param->notify.handle == hall_char_handle && param->notify.value_len >= 1) {
            cached_hall_state = param->notify.value[0];
            ESP_LOGI(TAG, "Hall notification: %d", cached_hall_state);
        }
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "Disconnected from FloraDock, reason=0x%x", param->disconnect.reason);

        set_state(STATE_IDLE);
        hall_char_handle = 0;
        hall_cccd_handle = 0;
        pump_char_handle = 0;
        service_start_handle = 0;
        service_end_handle = 0;

        xEventGroupSetBits(event_group, EVT_DISCONNECTED);
        xEventGroupClearBits(event_group, EVT_CONNECTED | EVT_SERVICE_FOUND | EVT_NOTIFY_ENABLED);
        break;

    default:
        break;
    }
}

static void reconnect_task(void *param)
{
    (void)param;
    int retry_count = 0;
    int delay_ms = 3000;

    while (1) {
        // Wait for disconnect event
        EventBits_t bits = xEventGroupWaitBits(event_group, EVT_DISCONNECTED,
                                                pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & EVT_DISCONNECTED) {
            if (!reconnect_enabled) {
                retry_count = 0;
                delay_ms = 3000;
                continue;
            }

            retry_count++;
            // Exponential backoff: 3s, 5s, 8s, 10s max
            delay_ms = (retry_count < 3) ? 3000 : (retry_count < 5) ? 5000 : (retry_count < 8) ? 8000 : 10000;

            ESP_LOGI(TAG, "Attempting reconnection (try %d) in %d ms...", retry_count, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));

            if (get_state() == STATE_IDLE && reconnect_enabled) {
                start_scan();
            }
        }

        // Reset retry count on successful connection
        if (get_state() == STATE_READY) {
            retry_count = 0;
            delay_ms = 3000;
        }
    }
}

void ble_dock_client_init(void)
{
    esp_err_t ret;

    // Create synchronization primitives
    event_group = xEventGroupCreate();
    if (event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    api_mutex = xSemaphoreCreateMutex();
    if (api_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Release classic BT memory (BLE only)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register callbacks
    ret = esp_ble_gattc_register_callback(gattc_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTC callback register failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GATTC app
    ret = esp_ble_gattc_app_register(GATTC_APP_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTC app register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Set MTU
    esp_ble_gatt_set_local_mtu(500);

    // Create reconnection task
    xTaskCreate(reconnect_task, "ble_reconnect", 2048, NULL, 5, &reconnect_task_handle);

    ESP_LOGI(TAG, "BLE dock client initialized");
}

bool ble_dock_is_ready(void)
{
    return get_state() == STATE_READY;
}

bool ble_dock_is_connected(void)
{
    client_state_t state = get_state();
    return state == STATE_CONNECTED || state == STATE_DISCOVERING || state == STATE_READY;
}

int ble_dock_get_hall_state(void)
{
    return (int)cached_hall_state;
}

esp_err_t ble_dock_read_hall(uint8_t *hall_state)
{
    if (hall_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (get_state() != STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(api_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    xEventGroupClearBits(event_group, EVT_READ_COMPLETE);

    esp_err_t ret = esp_ble_gattc_read_char(gattc_if_global, conn_id, hall_char_handle,
                                             ESP_GATT_AUTH_REQ_NONE);
    if (ret != ESP_OK) {
        xSemaphoreGive(api_mutex);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(event_group, EVT_READ_COMPLETE | EVT_DISCONNECTED,
                                            pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));

    xSemaphoreGive(api_mutex);

    if (bits & EVT_DISCONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!(bits & EVT_READ_COMPLETE)) {
        return ESP_ERR_TIMEOUT;
    }

    *hall_state = read_hall_value;
    return ESP_OK;
}

esp_err_t ble_dock_send_watering_ms(uint32_t duration_ms)
{
    if (get_state() != STATE_READY) {
        ESP_LOGW(TAG, "Not ready, state=%d", get_state());
        return ESP_ERR_INVALID_STATE;
    }

    if (pump_char_handle == 0) {
        ESP_LOGE(TAG, "Pump characteristic not found");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(api_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    xEventGroupClearBits(event_group, EVT_WRITE_COMPLETE);

    esp_err_t ret = esp_ble_gattc_write_char(gattc_if_global, conn_id, pump_char_handle,
                                              sizeof(uint32_t), (uint8_t *)&duration_ms,
                                              ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write char failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(api_mutex);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(event_group, EVT_WRITE_COMPLETE | EVT_DISCONNECTED,
                                            pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));

    xSemaphoreGive(api_mutex);

    if (bits & EVT_DISCONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!(bits & EVT_WRITE_COMPLETE)) {
        return ESP_ERR_TIMEOUT;
    }

    if (write_status != ESP_GATT_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ble_dock_run_watering_cycle(uint32_t pump_pulse_ms)
{
    if (!ble_dock_is_ready()) {
        ESP_LOGW(TAG, "Dock nie jest gotowy - brak polaczenia lub charakterystyk");
        return ESP_ERR_INVALID_STATE;
    }

    // Domyslny czas impulsu jesli nie podano (0 oznacza uzyj domyslnego)
    uint32_t actual_pulse_ms = pump_pulse_ms;
    if (actual_pulse_ms == 0) {
        actual_pulse_ms = 100;  // Domyslny 100ms
    }

    // Jesli duration >= 1000ms, to pojedynczy impuls bez petli (z MQTT)
    // Jesli duration < 1000ms, to petla z wieloma impulsami (z water_controller)
    bool single_impulse = (actual_pulse_ms >= 1000);

    if (single_impulse) {
        // Pojedynczy impuls bez petli - bezposrednio z MQTT
        ESP_LOGI(TAG, "Pojedynczy impuls podlewania: %lu ms (bez petli)", (unsigned long)actual_pulse_ms);
        esp_err_t err = ble_dock_send_watering_ms(actual_pulse_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Nie udalo sie wyslac komendy WATER: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Wyslano pojedynczy impuls WATER: %lu ms", (unsigned long)actual_pulse_ms);
        return ESP_OK;
    }

    // Petla z wieloma impulsami (z water_controller)
    sensor_soil_reading_t soil = {0};
    if (sensor_soil_read(&soil) != ESP_OK) {
        ESP_LOGW(TAG, "Blad odczytu wilgotnosci gleby");
        return ESP_FAIL;
    }

    float threshold = mqtt_get_soil_humidity_threshold();
    ESP_LOGI(TAG, "Start cyklu podlewania: wilgotnosc=%.2f%%, prog=%.2f%%, impuls=%lu ms",
             soil.moisture_percent, threshold, (unsigned long)actual_pulse_ms);

    if (soil.moisture_percent >= threshold) {
        ESP_LOGI(TAG, "Wilgotnosc juz wystarczajaca - pomijam podlewanie");
        return ESP_OK;
    }

    // Parametry petli
    const TickType_t settle_delay = pdMS_TO_TICKS(5000);   // czas na "dojscie" wody
    const int max_pulses = 30;                             // bezpieczny limit
    const int max_repositions = 5;

    int pulses_sent = 0;
    int repositions = 0;

    while (pulses_sent < max_pulses) {
        // 1) Krotki impuls wody
        esp_err_t err = ble_dock_send_watering_ms(actual_pulse_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Nie udalo sie wyslac komendy WATER: %s", esp_err_to_name(err));
            uint8_t hall = 0xFF;
            ble_dock_read_hall(&hall);
            ESP_LOGI(TAG, "Aktualny stan HALL z doku: %u (0=doniczka obecna, 1=brak)", hall);

            // Sprawdz czy doniczka nie jest na doku (hall == 1 oznacza brak doniczki)
            if (hall == 1 && motor_controller_is_initialized()) {
                // doniczka nie jest na doku - sprobuj przestawic wozek
                if (repositions >= max_repositions) {
                    ESP_LOGW(TAG, "Osiagnieto maksymalna liczbe prob przestawienia wozka");
                    break;
                }

                ESP_LOGW(TAG, "Hall == 1 (brak doniczki) - dojazd do przodu o 10cm i cofniecie do sciany");

                // 10 cm do przodu
                esp_err_t mret = motor_controller_move_distance("forward", 10.0f, 128);
                if (mret != ESP_OK) {
                    ESP_LOGE(TAG, "Blad ruchu w przod: %s", esp_err_to_name(mret));
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));

                // Jedz do tylu az do sciany (uzywajac logiki z water_controller)
                esp_err_t wall_res = water_controller_move_to_wall();
                if (wall_res != ESP_OK) {
                    ESP_LOGE(TAG, "Blad podczas jazdy do sciany: %s", esp_err_to_name(wall_res));
                    break;
                }

                repositions++;
                ESP_LOGI(TAG, "Wykonano przestawienie wozka (%d/%d)", repositions, max_repositions);
            }
        }
        pulses_sent++;
        ESP_LOGI(TAG, "Impuls WATER %d/%d: %lu ms", pulses_sent, max_pulses, (unsigned long)actual_pulse_ms);

        vTaskDelay(settle_delay);

        // 2) Ponowny pomiar wilgotnosci
        if (sensor_soil_read(&soil) != ESP_OK) {
            ESP_LOGW(TAG, "Blad odczytu wilgotnosci gleby po impulsie");
            continue;
        }

        ESP_LOGI(TAG, "Po impulsie: wilgotnosc=%.2f%% (prog=%.2f%%)",
                 soil.moisture_percent, threshold);

        if (soil.moisture_percent >= threshold) {
            ESP_LOGI(TAG, "Osiagnieto docelowa wilgotnosc - koniec cyklu");
            return ESP_OK;
        }

        // 3) Jesli wilgotnosc sie nie poprawia - sprawdz HALL (odczytaj aktualna wartosc)
        uint8_t hall = 0xFF;
        ble_dock_read_hall(&hall);
        ESP_LOGI(TAG, "Aktualny stan HALL z doku: %u (0=doniczka obecna, 1=brak)", hall);

        // Sprawdz czy doniczka nie jest na doku (hall == 1 oznacza brak doniczki)
        if (hall == 1 && motor_controller_is_initialized()) {
            // doniczka nie jest na doku - sprobuj przestawic wozek
            if (repositions >= max_repositions) {
                ESP_LOGW(TAG, "Osiagnieto maksymalna liczbe prob przestawienia wozka");
                break;
            }

            ESP_LOGW(TAG, "Hall == 1 (brak doniczki) - dojazd do przodu o 10cm i cofniecie do sciany");

            // 10 cm do przodu
            esp_err_t mret = motor_controller_move_distance("forward", 10.0f, 128);
            if (mret != ESP_OK) {
                ESP_LOGE(TAG, "Blad ruchu w przod: %s", esp_err_to_name(mret));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Jedz do tylu az do sciany (uzywajac logiki z water_controller)
            esp_err_t wall_res = water_controller_move_to_wall();
            if (wall_res != ESP_OK) {
                ESP_LOGE(TAG, "Blad podczas jazdy do sciany: %s", esp_err_to_name(wall_res));
                break;
            }

            repositions++;
            ESP_LOGI(TAG, "Wykonano przestawienie wozka (%d/%d)", repositions, max_repositions);
        }
    }

    ESP_LOGW(TAG, "Cykl podlewania zakonczony bez osiagniecia progu wilgotnosci");
    return ESP_FAIL;
}

void ble_dock_client_start_scan(void)
{
    if (get_state() == STATE_IDLE) {
        start_scan();
    }
}

void ble_dock_client_disconnect(void)
{
    reconnect_enabled = false;
    if (ble_dock_is_connected()) {
        esp_ble_gattc_close(gattc_if_global, conn_id);
    }
}

void ble_dock_client_enable_reconnect(bool enable)
{
    reconnect_enabled = enable;
    if (enable && get_state() == STATE_IDLE) {
        start_scan();
    }
}
