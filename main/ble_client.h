#pragma once

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

void init_ble(void);
void host_task(void *param);