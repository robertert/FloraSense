#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the BLE dock client
 *
 * Initializes the BLE stack and starts scanning for the FloraDock device.
 * Call this once during application startup.
 */
void ble_dock_client_init(void);

/**
 * @brief Check if the dock connection is ready for commands
 *
 * @return true if connected and service discovery is complete
 */
bool ble_dock_is_ready(void);

/**
 * @brief Check if connected to the dock (any connection state)
 *
 * @return true if connected (may still be discovering services)
 */
bool ble_dock_is_connected(void);

/**
 * @brief Get the cached hall sensor state
 *
 * Returns the last known hall state from notifications.
 * Does not perform a BLE read operation.
 *
 * @return 0 = pot present, 1 = pot absent
 */
int ble_dock_get_hall_state(void);

/**
 * @brief Read the hall sensor state synchronously
 *
 * Performs a BLE read operation and blocks until complete.
 *
 * @param hall_state Pointer to store the result (0=present, 1=absent)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ble_dock_read_hall(uint8_t *hall_state);

/**
 * @brief Send a watering command to the dock
 *
 * Tells the dock to activate the pump for the specified duration.
 * Blocks until the write is confirmed.
 *
 * @param duration_ms Duration in milliseconds (100-600000)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if pot not present
 */
esp_err_t ble_dock_send_watering_ms(uint32_t duration_ms);

/**
 * @brief Run a complete watering cycle
 *
 * Verifies pot presence, then sends the watering command.
 *
 * @param pulse_ms Duration of the watering pulse in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ble_dock_run_watering_cycle(uint32_t pulse_ms);

/**
 * @brief Manually start scanning for the dock
 *
 * Normally scanning starts automatically. Use this to restart
 * scanning if needed.
 */
void ble_dock_client_start_scan(void);

/**
 * @brief Disconnect from the dock
 *
 * Closes the connection and disables auto-reconnect.
 */
void ble_dock_client_disconnect(void);

/**
 * @brief Enable or disable automatic reconnection
 *
 * @param enable true to enable auto-reconnect, false to disable
 */
void ble_dock_client_enable_reconnect(bool enable);

#ifdef __cplusplus
}
#endif
