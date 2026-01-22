#include "ble_server.h"
#include "dock_control.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ble_server";

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

#define GATTS_APP_ID            0
#define DEVICE_NAME             "FloraDock"
#define GATTS_NUM_HANDLES       8

// Characteristic properties
#define HALL_CHAR_PROP          (ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY)
#define PUMP_CHAR_PROP          (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)

// CCCD value for notifications enabled
static uint8_t cccd_value[2] = {0x00, 0x00};

// GATT handles
static uint16_t service_handle = 0;
static uint16_t hall_char_handle = 0;
static uint16_t hall_cccd_handle = 0;
static uint16_t pump_char_handle = 0;
static esp_gatt_if_t gatts_if_global = ESP_GATT_IF_NONE;

// Connection state
static uint16_t conn_id = 0;
static bool is_connected = false;
static portMUX_TYPE ble_lock = portMUX_INITIALIZER_UNLOCKED;

// Hall state cache
static uint8_t current_hall_state = 1;

// Callback for water commands
static ble_server_water_cmd_cb_t water_cmd_callback = NULL;

// Advertising data
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
    .service_uuid_len = sizeof(service_uuid),
    .p_service_uuid = service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// Scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// Advertising parameters
// Intervals: 0x20=20ms, 0x30=30ms, 0xA0=100ms, 0x140=200ms
// Using moderate intervals for reliability
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x40,  // 40ms
    .adv_int_max = 0x80,  // 80ms
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_advertising(void)
{
    esp_err_t ret = esp_ble_gap_start_advertising(&adv_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start advertising failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Advertising started");
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising data set complete");
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan response data set complete");
        start_advertising();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising stop failed: %d", param->adv_stop_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Connection params updated: status=%d, interval=%d",
                 param->update_conn_params.status, param->update_conn_params.conn_int);
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            gatts_if_global = gatts_if;
            ESP_LOGI(TAG, "GATTS app registered, app_id=%d", param->reg.app_id);

            // Set device name
            esp_ble_gap_set_device_name(DEVICE_NAME);

            // Configure advertising data
            esp_ble_gap_config_adv_data(&adv_data);
            esp_ble_gap_config_adv_data(&scan_rsp_data);

            // Create service
            esp_gatt_srvc_id_t service_id = {
                .is_primary = true,
                .id = {
                    .inst_id = 0,
                    .uuid = {
                        .len = ESP_UUID_LEN_128,
                    },
                },
            };
            memcpy(service_id.id.uuid.uuid.uuid128, service_uuid, 16);
            esp_ble_gatts_create_service(gatts_if, &service_id, GATTS_NUM_HANDLES);
        } else {
            ESP_LOGE(TAG, "GATTS register failed: %d", param->reg.status);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        if (param->create.status == ESP_GATT_OK) {
            service_handle = param->create.service_handle;
            ESP_LOGI(TAG, "Service created, handle=%d", service_handle);

            // Start service
            esp_ble_gatts_start_service(service_handle);

            // Add Hall Sensor characteristic
            esp_bt_uuid_t hall_uuid = {
                .len = ESP_UUID_LEN_128,
            };
            memcpy(hall_uuid.uuid.uuid128, hall_char_uuid, 16);

            esp_attr_value_t hall_val = {
                .attr_max_len = sizeof(uint8_t),
                .attr_len = sizeof(uint8_t),
                .attr_value = &current_hall_state,
            };

            esp_attr_control_t hall_ctrl = {
                .auto_rsp = ESP_GATT_RSP_BY_APP,
            };

            esp_ble_gatts_add_char(service_handle, &hall_uuid, ESP_GATT_PERM_READ,
                                   HALL_CHAR_PROP, &hall_val, &hall_ctrl);
        }
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        if (param->add_char.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Characteristic added, handle=%d", param->add_char.attr_handle);

            // Check which characteristic was added by comparing UUIDs
            if (hall_char_handle == 0) {
                hall_char_handle = param->add_char.attr_handle;
                ESP_LOGI(TAG, "Hall characteristic handle: %d", hall_char_handle);

                // Add CCCD descriptor for notifications
                esp_bt_uuid_t cccd_uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG},
                };

                esp_attr_value_t cccd_val = {
                    .attr_max_len = sizeof(cccd_value),
                    .attr_len = sizeof(cccd_value),
                    .attr_value = cccd_value,
                };

                esp_ble_gatts_add_char_descr(service_handle, &cccd_uuid,
                                             ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                             &cccd_val, NULL);
            } else {
                pump_char_handle = param->add_char.attr_handle;
                ESP_LOGI(TAG, "Pump characteristic handle: %d", pump_char_handle);
            }
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        if (param->add_char_descr.status == ESP_GATT_OK) {
            hall_cccd_handle = param->add_char_descr.attr_handle;
            ESP_LOGI(TAG, "CCCD descriptor added, handle=%d", hall_cccd_handle);

            // Now add the Pump Control characteristic
            esp_bt_uuid_t pump_uuid = {
                .len = ESP_UUID_LEN_128,
            };
            memcpy(pump_uuid.uuid.uuid128, pump_char_uuid, 16);

            uint32_t pump_init = 0;
            esp_attr_value_t pump_val = {
                .attr_max_len = sizeof(uint32_t),
                .attr_len = sizeof(uint32_t),
                .attr_value = (uint8_t *)&pump_init,
            };

            esp_attr_control_t pump_ctrl = {
                .auto_rsp = ESP_GATT_RSP_BY_APP,
            };

            esp_ble_gatts_add_char(service_handle, &pump_uuid,
                                   ESP_GATT_PERM_WRITE,
                                   PUMP_CHAR_PROP, &pump_val, &pump_ctrl);
        }
        break;

    case ESP_GATTS_START_EVT:
        if (param->start.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Service started");
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        taskENTER_CRITICAL(&ble_lock);
        conn_id = param->connect.conn_id;
        is_connected = true;
        taskEXIT_CRITICAL(&ble_lock);

        ESP_LOGI(TAG, "Client connected, conn_id=%d, remote_bda=" ESP_BD_ADDR_STR,
                 conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));

        // Stop advertising while connected
        esp_ble_gap_stop_advertising();

        // Update connection parameters for faster response
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;  // 40ms
        conn_params.min_int = 0x10;  // 20ms
        conn_params.timeout = 400;   // 4s
        esp_ble_gap_update_conn_params(&conn_params);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        taskENTER_CRITICAL(&ble_lock);
        is_connected = false;
        taskEXIT_CRITICAL(&ble_lock);

        ESP_LOGI(TAG, "Client disconnected, reason=0x%x", param->disconnect.reason);

        // Reset CCCD
        cccd_value[0] = 0x00;
        cccd_value[1] = 0x00;

        // Restart advertising
        start_advertising();
        break;

    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG, "Read request, handle=%d", param->read.handle);

        if (param->read.handle == hall_char_handle) {
            esp_gatt_rsp_t rsp = {0};
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.len = sizeof(uint8_t);
            rsp.attr_value.value[0] = dock_control_get_hall_state();
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id, ESP_GATT_OK, &rsp);
        } else if (param->read.handle == hall_cccd_handle) {
            esp_gatt_rsp_t rsp = {0};
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.len = sizeof(cccd_value);
            memcpy(rsp.attr_value.value, cccd_value, sizeof(cccd_value));
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id, ESP_GATT_OK, &rsp);
        }
        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "Write request, handle=%d, len=%d", param->write.handle, param->write.len);

        if (param->write.handle == pump_char_handle) {
            if (param->write.len == sizeof(uint32_t)) {
                uint32_t duration_ms = 0;
                memcpy(&duration_ms, param->write.value, sizeof(uint32_t));
                ESP_LOGI(TAG, "Water command received: %lu ms", (unsigned long)duration_ms);

                esp_err_t result = ESP_FAIL;
                if (water_cmd_callback) {
                    result = water_cmd_callback(duration_ms);
                }

                if (!param->write.need_rsp) {
                    break;
                }

                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id,
                                            result == ESP_OK ? ESP_GATT_OK : ESP_GATT_ERROR,
                                            NULL);
            } else {
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                param->write.trans_id, ESP_GATT_INVALID_ATTR_LEN, NULL);
                }
            }
        } else if (param->write.handle == hall_cccd_handle) {
            if (param->write.len == 2) {
                memcpy(cccd_value, param->write.value, 2);
                ESP_LOGI(TAG, "CCCD written: 0x%02x%02x (notifications %s)",
                         cccd_value[1], cccd_value[0],
                         (cccd_value[0] & 0x01) ? "enabled" : "disabled");
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, NULL);
            }
        }
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU updated to %d", param->mtu.mtu);
        break;

    case ESP_GATTS_CONF_EVT:
        if (param->conf.status != ESP_GATT_OK) {
            ESP_LOGW(TAG, "Notification/indication confirm failed: %d", param->conf.status);
        }
        break;

    default:
        break;
    }
}

void ble_server_init(void)
{
    esp_err_t ret;

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
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS callback register failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GATTS app
    ret = esp_ble_gatts_app_register(GATTS_APP_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Set MTU
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "BLE server initialized");
}

void ble_server_register_handlers(ble_server_water_cmd_cb_t water_cb)
{
    water_cmd_callback = water_cb;
    ESP_LOGI(TAG, "Water command handler registered");
}

void ble_server_set_hall_state(uint8_t hall_state)
{
    current_hall_state = hall_state;

    taskENTER_CRITICAL(&ble_lock);
    bool connected = is_connected;
    uint16_t current_conn_id = conn_id;
    taskEXIT_CRITICAL(&ble_lock);

    // Send notification if connected and notifications enabled
    if (connected && (cccd_value[0] & 0x01) && gatts_if_global != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(gatts_if_global, current_conn_id, hall_char_handle,
                                    sizeof(uint8_t), &hall_state, false);
        ESP_LOGI(TAG, "Hall state notification sent: %d", hall_state);
    }
}

bool ble_server_is_connected(void)
{
    taskENTER_CRITICAL(&ble_lock);
    bool connected = is_connected;
    taskEXIT_CRITICAL(&ble_lock);
    return connected;
}
