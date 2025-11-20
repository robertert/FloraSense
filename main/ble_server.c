#include "ble_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"

static const char *TAG = "BLE_SERVER";

// UUID usługi i charakterystyki
#define SERVICE_UUID       0xFFE0
#define CHARACTERISTIC_UUID 0xFFE1

// GATT profile
static uint16_t gatt_service_handle = 0;
static uint16_t gatt_char_handle = 0;
static esp_gatt_if_t gatt_if_global = 0;

static uint8_t char_value[20] = {'H','e','l','l','o'};
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/// ---- HANDLERY ZDARZEŃ GATT SERVER ----
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "Zarejestrowano GATT interface");

        // Tworzymy tablicę service: PRIMARY SERVICE
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = {
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = SERVICE_UUID }
                },
                .inst_id = 0
            }
        };

        esp_ble_gatts_create_service(gatts_if, &service_id, 4);
        gatt_if_global = gatts_if;
        break;


    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "Utworzono usługę, handle=%d", param->create.service_handle);
        gatt_service_handle = param->create.service_handle;

        // Dodajemy charakterystykę
        esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_READ |
                                    ESP_GATT_CHAR_PROP_BIT_WRITE |
                                    ESP_GATT_CHAR_PROP_BIT_NOTIFY;

        esp_attr_control_t control = { .auto_rsp = ESP_GATT_AUTO_RSP };

        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = CHARACTERISTIC_UUID }
        };

        esp_attr_value_t char_val = {
            .attr_max_len = sizeof(char_value),
            .attr_len     = sizeof(char_value),
            .attr_value   = char_value,
        };

        esp_ble_gatts_add_char(gatt_service_handle,
                               &char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               prop,
                               &char_val,
                               &control);
        break;


    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(TAG, "Dodano charakterystykę, handle=%d", param->add_char.attr_handle);
        gatt_char_handle = param->add_char.attr_handle;
        esp_ble_gatts_start_service(gatt_service_handle);
        break;


    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "WRITE: len=%d", param->write.len);

        ESP_LOGI(TAG, "WRITE: value=%s", param->write.value);
        
        if (param->write.len <= sizeof(char_value)) {
            memcpy(char_value, param->write.value, param->write.len);
        }

        // Odpowiadamy notify
        esp_ble_gatts_send_indicate(
            gatts_if,
            param->write.conn_id,
            gatt_char_handle,
            param->write.len,
            param->write.value,
            false
        );
        break;


    default:
        break;
    }
}


/// ---- GAP EVENTY (reklamowanie) ----
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


/// ---- FUNKCJA STARTOWA ----
void ble_server_init(void)
{
    ESP_LOGI(TAG, "Init BLE Server...");

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Rejestracja callbacków
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    esp_ble_gatts_app_register(0);

    // Dane reklamowe
    static uint8_t adv_service_uuid128[16] = {0xE0, 0xFF}; // FFE0
    static esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = false,
        .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
        .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
        .appearance = 0x00,
        .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
        .p_manufacturer_data =  NULL, //&test_manufacturer[0],
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(adv_service_uuid128),
        .p_service_uuid = adv_service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name("ESP32-Server"));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
}