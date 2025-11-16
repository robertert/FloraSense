#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"

static const char *TAG = "ITAG_CLIENT";

// UUID serwisu i charakterystyki (Battery Service)
static const uint16_t BATTERY_SERVICE_UUID = 0x180F;
static const uint16_t BATTERY_LEVEL_UUID   = 0x2A19;

static esp_gatt_if_t gattc_if_global = 0;
static uint16_t conn_id = 0;
static bool itag_found = false;
static esp_bd_addr_t itag_address;

static void start_scan(void);

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch(event) {
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            esp_ble_gap_cb_param_t *scan_res = &param->scan_rst;

            if (scan_res->search_evt == ESP_GAP_SEARCH_INQ_RES) {
                uint8_t *adv_name = NULL;
                uint8_t adv_name_len = 0;

                adv_name = esp_ble_resolve_adv_data(scan_res->ble_adv,
                                                    ESP_BLE_AD_TYPE_NAME_CMPL,
                                                    &adv_name_len);
                if (adv_name && adv_name_len) {
                    if (strncmp((char*)adv_name, "iTag", adv_name_len) == 0) {
                        ESP_LOGI(TAG, "Znaleziono iTag!");
                        memcpy(itag_address, scan_res->bda, 6);
                        itag_found = true;
                        esp_ble_gap_stop_scanning();
                    }
                }
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if (itag_found) {
                ESP_LOGI(TAG, "Łączenie z iTag...");
                esp_ble_gattc_open(gattc_if_global, itag_address, true);
            }
            break;
        default:
            break;
    }
}

static void gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                      esp_ble_gattc_cb_param_t *param) {
    switch(event) {
        case ESP_GATTC_REG_EVT:
            gattc_if_global = gattc_if;
            start_scan();
            break;

        case ESP_GATTC_OPEN_EVT:
            if (param->open.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "Połączono z iTag");
                conn_id = param->open.conn_id;
                esp_ble_gattc_search_service(gattc_if, conn_id, NULL);
            } else {
                ESP_LOGE(TAG, "Błąd połączenia");
            }
            break;

        case ESP_GATTC_SEARCH_RES_EVT: {
            esp_gatt_srvc_id_t *service = &param->search_res.srvc_id;
            if (service->id.uuid.len == ESP_UUID_LEN_16 &&
                service->id.uuid.uuid.uuid16 == BATTERY_SERVICE_UUID) {
                ESP_LOGI(TAG, "Znaleziono Battery Service");

                esp_bt_uuid_t char_uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = {.uuid16 = BATTERY_LEVEL_UUID}
                };
                esp_ble_gattc_get_characteristic(gattc_if, conn_id, service, &char_uuid);
            }
            break;
        }

        case ESP_GATTC_GET_CHAR_EVT:
            if (param->get_char.char_uuid.uuid.uuid16 == BATTERY_LEVEL_UUID) {
                ESP_LOGI(TAG, "Czytam Battery Level...");
                esp_ble_gattc_read_char(gattc_if, conn_id,
                                        param->get_char.char_handle,
                                        ESP_GATT_AUTH_REQ_NONE);
            }
            break;

        case ESP_GATTC_READ_CHAR_EVT:
            if (param->read.status == ESP_GATT_OK) {
                uint8_t battery = param->read.value[0];
                ESP_LOGI(TAG, "Poziom baterii iTag: %d%%", battery);
            } else {
                ESP_LOGE(TAG, "Błąd odczytu charakterystyki");
            }
            break;

        default:
            break;
    }
}

static void start_scan(void) {
    esp_ble_gap_set_scan_params(& (esp_ble_scan_params_t) {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30
    });
}

void ble_init(void) {
    ESP_LOGI(TAG, "Uruchamianie GATT Client...");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
}