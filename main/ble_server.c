#include <string.h>
#include "ble_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"

static const char *TAG = "BLE_SERVER";

#define DOCK_SERVICE_INST_ID     0
#define DOCK_DEVICE_NAME         "FloraDock"

enum dock_gatt_idx {
    IDX_SVC = 0,
    IDX_CHAR_HALL,
    IDX_CHAR_HALL_VAL,
    IDX_CHAR_HALL_CCC,
    IDX_CHAR_HALL_USER_DESC,
    IDX_CHAR_WATER,
    IDX_CHAR_WATER_VAL,
    IDX_CHAR_WATER_USER_DESC,
    FLORA_IDX_NB,
};

static uint16_t handle_table[FLORA_IDX_NB];
static esp_gatt_if_t gatt_if_global = ESP_GATT_IF_NONE;
static bool attr_table_ready = false;
static bool device_connected = false;
static uint16_t connection_id = 0xFFFF;
static bool hall_notify_enabled = false;
static uint8_t hall_value[1] = {0};
static ble_server_water_cmd_cb_t water_cmd_cb = NULL;

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t client_char_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t user_desc_uuid = ESP_GATT_UUID_CHAR_DESCRIPTION;

static const uint8_t dock_service_uuid[ESP_UUID_LEN_128] = {
    0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xFF, 0xEE,
    0xDD, 0xCC, 0xBB, 0xAA, 0x00, 0x56, 0x34, 0x12
};

static const uint8_t hall_char_uuid[ESP_UUID_LEN_128] = {
    0xA1, 0x01, 0x00, 0x00, 0x55, 0x55, 0x44, 0x44,
    0x33, 0x33, 0x22, 0x22, 0x11, 0x11, 0x10, 0x01
};

static const uint8_t water_char_uuid[ESP_UUID_LEN_128] = {
    0xA1, 0x02, 0x00, 0x00, 0x55, 0x55, 0x44, 0x44,
    0x33, 0x33, 0x22, 0x22, 0x11, 0x11, 0x10, 0x02
};

static const uint8_t char_prop_read_notify[] = {ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY};
static const uint8_t char_prop_read_write[] = {ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR};

// Opisy wyświetlane np. w nRF Connect jako „User Description”
static const char hall_user_desc[]  = "Stan doniczki (Hall)";
static const char water_user_desc[] = "Podlewanie (czas w ms)";

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

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
    .service_uuid_len = sizeof(dock_service_uuid),
    .p_service_uuid = (uint8_t *)dock_service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static const esp_gatts_attr_db_t dock_gatt_db[FLORA_IDX_NB] = {
    [IDX_SVC] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&primary_service_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = ESP_UUID_LEN_128,
            .length = ESP_UUID_LEN_128,
            .value = (uint8_t *)dock_service_uuid,
        },
    },
    [IDX_CHAR_HALL] = {
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
    [IDX_CHAR_HALL_VAL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_128,
            .uuid_p = (uint8_t *)hall_char_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(hall_value),
            .length = sizeof(hall_value),
            .value = hall_value,
        },
    },
    [IDX_CHAR_HALL_CCC] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&client_char_config_uuid,
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = sizeof(uint16_t),
            .length = sizeof(uint16_t),
            .value = (uint8_t[]){0x00, 0x00},
        },
    },
    [IDX_CHAR_HALL_USER_DESC] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&user_desc_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(hall_user_desc),
            .length = sizeof(hall_user_desc),
            .value = (uint8_t *)hall_user_desc,
        },
    },
    [IDX_CHAR_WATER] = {
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
    [IDX_CHAR_WATER_VAL] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_128,
            .uuid_p = (uint8_t *)water_char_uuid,
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = sizeof(uint32_t),
            .length = sizeof(uint32_t),
            .value = (uint8_t[]){0x00, 0x00, 0x00, 0x00},
        },
    },
    [IDX_CHAR_WATER_USER_DESC] = {
        .attr_control = {ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&user_desc_uuid,
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(water_user_desc),
            .length = sizeof(water_user_desc),
            .value = (uint8_t *)water_user_desc,
        },
    },
};

void ble_server_register_handlers(ble_server_water_cmd_cb_t water_cb)
{
    water_cmd_cb = water_cb;
}

static void apply_hall_value(void)
{
    if (!attr_table_ready) {
        return;
    }
    esp_ble_gatts_set_attr_value(handle_table[IDX_CHAR_HALL_VAL], sizeof(hall_value), hall_value);
    if (hall_notify_enabled && device_connected) {
        esp_ble_gatts_send_indicate(
            gatt_if_global,
            connection_id,
            handle_table[IDX_CHAR_HALL_VAL],
            sizeof(hall_value),
            hall_value,
            false);
    }
}

void ble_server_set_hall_state(uint8_t new_state)
{
    // Przekazujemy stan 0/1 dokładnie tak, jak dostajemy z dock_control:
    // 0 = doniczka OBECNA, 1 = doniczka BRAK.
    hall_value[0] = new_state ? 1 : 0;
    apply_hall_value();
}

static void handle_water_write(const esp_ble_gatts_cb_param_t *param)
{
    uint32_t duration_ms = 0;
    if (param->write.len >= 4) {
        duration_ms = param->write.value[0] |
                      (param->write.value[1] << 8) |
                      (param->write.value[2] << 16) |
                      (param->write.value[3] << 24);
    } else if (param->write.len >= 2) {
        duration_ms = param->write.value[0] |
                      (param->write.value[1] << 8);
    } else if (param->write.len == 1) {
        duration_ms = param->write.value[0] * 100;
    } else {
        ESP_LOGW(TAG, "Pusta komenda water");
        return;
    }

    esp_ble_gatts_set_attr_value(handle_table[IDX_CHAR_WATER_VAL], param->write.len, param->write.value);

    // 0 = doniczka obecna, tylko wtedy wolno podlewać
    if (hall_value[0] != 0) {
        ESP_LOGW(TAG, "Komenda water odrzucona: brak doniczki (Hall=%u)", hall_value[0]);
        return;
    }

    if (water_cmd_cb) {
        esp_err_t res = water_cmd_cb(duration_ms);
        ESP_LOGI(TAG, "Komenda water dur=%u ms wynik=%s", duration_ms, esp_err_to_name(res));
    } else {
        ESP_LOGW(TAG, "Brak zarejestrowanego handlera water");
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "Zarejestrowano GATT interface");
        gatt_if_global = gatts_if;
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name(DOCK_DEVICE_NAME));
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
        ESP_ERROR_CHECK(esp_ble_gatts_create_attr_tab(dock_gatt_db, gatts_if, FLORA_IDX_NB, DOCK_SERVICE_INST_ID));
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
        apply_hall_value();
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
        hall_notify_enabled = false;
        connection_id = 0xFFFF;
        ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&adv_params));
        break;

    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            if (param->write.handle == handle_table[IDX_CHAR_HALL_CCC] && param->write.len >= 2) {
                uint16_t ccc_val = param->write.value[1] << 8 | param->write.value[0];
                hall_notify_enabled = (ccc_val == 0x0001);
                ESP_LOGI(TAG, "Powiadomienia HALL %s", hall_notify_enabled ? "WLACZONE" : "WYLACZONE");
            } else if (param->write.handle == handle_table[IDX_CHAR_WATER_VAL]) {
                handle_water_write(param);
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

    esp_ble_gatts_app_register(DOCK_SERVICE_INST_ID);
}