#include <string.h>
#include "ble_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"

static const char *TAG = "BLE_SERVER";

#define FLORA_SERVICE_INST_ID     0
#define FLORA_DEVICE_NAME         "FloraSense"

enum flora_gatt_idx {
    IDX_SVC = 0,
    IDX_CHAR_TEMP,
    IDX_CHAR_TEMP_VAL,
    IDX_CHAR_TEMP_CCC,
    IDX_CHAR_SOIL,
    IDX_CHAR_SOIL_VAL,
    IDX_CHAR_LIGHT,
    IDX_CHAR_LIGHT_VAL,
    IDX_CHAR_PUMP,
    IDX_CHAR_PUMP_VAL,
    FLORA_IDX_NB,
};

static uint16_t handle_table[FLORA_IDX_NB];
static esp_gatt_if_t gatt_if_global = ESP_GATT_IF_NONE;
static bool attr_table_ready = false;
static bool device_connected = false;
static uint16_t connection_id = 0xFFFF;
static bool temp_notify_enabled = false;
static ble_server_pump_command_cb_t pump_callback = NULL;
static ble_server_measurements_t latest_measurements = {
    .temperature_c = 23.5f,
    .soil_moisture_pct = 50,
    .light_lux = 400,
};

static uint8_t temperature_value[2] = {0x00, 0x00}; // 0.01 stopnia C
static uint8_t soil_moisture_value[2] = {50, 0x00}; // wilgotność w %
static uint8_t light_value[2] = {0x90, 0x01};       // luks
static uint8_t pump_state_value[1] = {0x00};
static uint16_t temp_ccc = 0x0000;

// UUIDy standardowe
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t client_char_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t environmental_sensing_uuid = ESP_GATT_UUID_ENVIRONMENTAL_SENSING_SVC;
static const uint16_t temperature_char_uuid = 0x2A6E; // Temperature
static const uint16_t soil_moisture_char_uuid = 0x2F7A; // Soil Moisture
static const uint16_t light_char_uuid = 0x2AFB; // Illuminance

static const uint8_t pump_char_uuid[ESP_UUID_LEN_128] = {
    0x30, 0x9C, 0xAA, 0x80, 0x95, 0xD4, 0x4E, 0xCC,
    0xB5, 0x59, 0x04, 0xD2, 0x02, 0xFA, 0x71, 0x44
};

static const uint8_t char_prop_read[] = {ESP_GATT_CHAR_PROP_BIT_READ};
static const uint8_t char_prop_read_notify[] = {ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY};
static const uint8_t char_prop_read_write[] = {ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_service_uuid128[ESP_UUID_LEN_128] = {
    0xFB, 0x34, 0x9B, 0x5F,
    0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00,
    0x1A, 0x18, 0x00, 0x00,
}; // 0000181a-0000-1000-8000-00805F9B34FB (Environmental Sensing)
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static const esp_gatts_attr_db_t flora_gatt_db[FLORA_IDX_NB] = {
    [IDX_SVC] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&primary_service_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint16_t),
            .length = sizeof(environmental_sensing_uuid),
            .value = (uint8_t *)&environmental_sensing_uuid,
        },
    },
    [IDX_CHAR_TEMP] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&character_declaration_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t),
            .length = sizeof(uint8_t),
            .value = (uint8_t *)char_prop_read_notify,
        },
    },
    [IDX_CHAR_TEMP_VAL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&temperature_char_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(temperature_value),
            .length = sizeof(temperature_value),
            .value = temperature_value,
        },
    },
    [IDX_CHAR_TEMP_CCC] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&client_char_config_uuid,
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = sizeof(uint16_t),
            .length = sizeof(uint16_t),
            .value = (uint8_t *)&temp_ccc,
        },
    },
    [IDX_CHAR_SOIL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&character_declaration_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t),
            .length = sizeof(uint8_t),
            .value = (uint8_t *)char_prop_read,
        },
    },
    [IDX_CHAR_SOIL_VAL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&soil_moisture_char_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(soil_moisture_value),
            .length = sizeof(soil_moisture_value),
            .value = soil_moisture_value,
        },
    },
    [IDX_CHAR_LIGHT] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&character_declaration_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t),
            .length = sizeof(uint8_t),
            .value = (uint8_t *)char_prop_read,
        },
    },
    [IDX_CHAR_LIGHT_VAL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&light_char_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(light_value),
            .length = sizeof(light_value),
            .value = light_value,
        },
    },
    [IDX_CHAR_PUMP] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&character_declaration_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t),
            .length = sizeof(uint8_t),
            .value = (uint8_t *)char_prop_read_write,
        },
    },
    [IDX_CHAR_PUMP_VAL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_128,
            .uuid_p = (uint8_t *)pump_char_uuid,
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = sizeof(pump_state_value),
            .length = sizeof(pump_state_value),
            .value = pump_state_value,
        },
    },
};

static void encode_measurements(void)
{
    int16_t temp_centi = (int16_t)(latest_measurements.temperature_c * 100.0f);
    temperature_value[0] = (uint8_t)(temp_centi & 0xFF);
    temperature_value[1] = (uint8_t)((temp_centi >> 8) & 0xFF);

    uint16_t soil = latest_measurements.soil_moisture_pct;
    soil_moisture_value[0] = (uint8_t)(soil & 0xFF);
    soil_moisture_value[1] = (uint8_t)((soil >> 8) & 0xFF);

    uint16_t light = latest_measurements.light_lux;
    light_value[0] = (uint8_t)(light & 0xFF);
    light_value[1] = (uint8_t)((light >> 8) & 0xFF);
}

static void apply_measurements_to_attrs(void)
{
    if (!attr_table_ready) {
        return;
    }

    encode_measurements();
    esp_ble_gatts_set_attr_value(handle_table[IDX_CHAR_TEMP_VAL], sizeof(temperature_value), temperature_value);
    esp_ble_gatts_set_attr_value(handle_table[IDX_CHAR_SOIL_VAL], sizeof(soil_moisture_value), soil_moisture_value);
    esp_ble_gatts_set_attr_value(handle_table[IDX_CHAR_LIGHT_VAL], sizeof(light_value), light_value);

    if (temp_notify_enabled && device_connected) {
        esp_ble_gatts_send_indicate(
            gatt_if_global,
            connection_id,
            handle_table[IDX_CHAR_TEMP_VAL],
            sizeof(temperature_value),
            temperature_value,
            false);
    }
}

void ble_server_update_measurements(const ble_server_measurements_t *measurements)
{
    if (!measurements) {
        return;
    }

    latest_measurements = *measurements;
    apply_measurements_to_attrs();
    ESP_LOGI(TAG, "Zaktualizowano pomiary BLE: T=%.2fC, wilg=%u%%, lux=%u",
             latest_measurements.temperature_c,
             latest_measurements.soil_moisture_pct,
             latest_measurements.light_lux);
}

void ble_server_register_pump_callback(ble_server_pump_command_cb_t callback)
{
    pump_callback = callback;
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "Zarejestrowano GATT interface");
        gatt_if_global = gatts_if;
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name(FLORA_DEVICE_NAME));
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
        ESP_ERROR_CHECK(esp_ble_gatts_create_attr_tab(flora_gatt_db, gatts_if, FLORA_IDX_NB, FLORA_SERVICE_INST_ID));
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Nie udalo sie stworzyc tablicy atrybutow, status=0x%x", param->add_attr_tab.status);
            break;
        }
        memcpy(handle_table, param->add_attr_tab.handles, sizeof(handle_table));
        attr_table_ready = true;
        ESP_LOGI(TAG, "Tabela atrybutow gotowa, start uslugi");
        esp_ble_gatts_start_service(handle_table[IDX_SVC]);
        apply_measurements_to_attrs();
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "Usluga BLE wystartowala");
        break;

    case ESP_GATTS_CONNECT_EVT:
        device_connected = true;
        connection_id = param->connect.conn_id;
        ESP_LOGI(TAG, "Klient polaczony, conn_id=%d", connection_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Klient rozlaczony");
        device_connected = false;
        temp_notify_enabled = false;
        connection_id = 0xFFFF;
        ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&adv_params));
        break;

    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            if (param->write.handle == handle_table[IDX_CHAR_TEMP_CCC]) {
                uint16_t ccc_val = param->write.value[1] << 8 | param->write.value[0];
                temp_notify_enabled = (ccc_val == 0x0001);
                ESP_LOGI(TAG, "Powiadomienia temperatury %s", temp_notify_enabled ? "WLACZONE" : "WYLACZONE");
            } else if (param->write.handle == handle_table[IDX_CHAR_PUMP_VAL] && param->write.len >= 1) {
                pump_state_value[0] = param->write.value[0] ? 1 : 0;
                esp_ble_gatts_set_attr_value(handle_table[IDX_CHAR_PUMP_VAL], sizeof(pump_state_value), pump_state_value);
                ESP_LOGI(TAG, "Komenda pompy: %s", pump_state_value[0] ? "START" : "STOP");
                if (pump_callback) {
                    pump_callback(pump_state_value[0] == 1);
                }
            }
        }
        break;

    default:
        break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Dane advertising skonfigurowane, start reklamowania");
        ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&adv_params));
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Reklamowanie wystartowało");
        } else {
            ESP_LOGE(TAG, "Nie udało się wystartować reklamowania, status=0x%x",
                     param->adv_start_cmpl.status);
        }
        break;

    default:
        break;
    }
}

void ble_server_init(void)
{
    ESP_LOGI(TAG, "Init BLE Server...");

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    esp_ble_gatts_app_register(FLORA_SERVICE_INST_ID);
}