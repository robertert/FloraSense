/**
 * @file mpu6050.h
 * @brief ESP-IDF driver library for MPU6050 6-axis MotionTracking device
 * 
 * This library provides comprehensive control over the MPU6050 sensor including:
 * - Initialization and configuration
 * - Data reading and conversion to physical units
 * - FIFO support
 * - Interrupt configuration
 * - Power management
 * - Offset calibration
 * - Auxiliary I2C support for magnetometer
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MPU6050 device handle structure
 */
typedef struct {
    i2c_master_bus_handle_t bus_handle;  ///< I2C bus handle
    i2c_master_dev_handle_t dev_handle;  ///< I2C device handle
    uint8_t i2c_address;                 ///< I2C device address (typically 0x68 or 0x69)
    bool initialized;                     ///< Initialization status
} mpu6050_handle_t;

/**
 * @brief MPU6050 initialization configuration structure
 */
typedef struct {
    i2c_port_t i2c_port;          ///< I2C port number (I2C_NUM_0 or I2C_NUM_1)
    uint8_t i2c_address;          ///< I2C device address (0x68 or 0x69, default: 0x68)
    gpio_num_t sda_pin;           ///< SDA GPIO pin number
    gpio_num_t scl_pin;           ///< SCL GPIO pin number
    uint32_t i2c_freq_hz;         ///< I2C clock frequency in Hz (default: 400000)
    bool enable_internal_pullup;  ///< Enable internal pull-up resistors
} mpu6050_config_t;

/**
 * @brief Clock source selection (PWR_MGMT_1 register, bits 2:0)
 */
typedef enum {
    MPU6050_CLK_INTERNAL_8MHZ = 0,        ///< Internal 8MHz oscillator
    MPU6050_CLK_PLL_X_GYRO = 1,           ///< PLL with X-axis gyro reference (recommended)
    MPU6050_CLK_PLL_Y_GYRO = 2,           ///< PLL with Y-axis gyro reference
    MPU6050_CLK_PLL_Z_GYRO = 3,           ///< PLL with Z-axis gyro reference
    MPU6050_CLK_PLL_EXT_32KHZ = 4,        ///< PLL with external 32.768kHz reference
    MPU6050_CLK_PLL_EXT_19MHZ = 5,        ///< PLL with external 19.2MHz reference
    MPU6050_CLK_STOP = 7                  ///< Stop clock and keep timing generator
} mpu6050_clock_source_t;

/**
 * @brief Gyroscope full scale range (GYRO_CONFIG register, bits 4:3)
 */
typedef enum {
    MPU6050_GYRO_FS_250DPS = 0,   ///< +/- 250 degrees per second
    MPU6050_GYRO_FS_500DPS = 1,    ///< +/- 500 degrees per second
    MPU6050_GYRO_FS_1000DPS = 2,   ///< +/- 1000 degrees per second
    MPU6050_GYRO_FS_2000DPS = 3    ///< +/- 2000 degrees per second
} mpu6050_gyro_fs_t;

/**
 * @brief Accelerometer full scale range (ACCEL_CONFIG register, bits 4:3)
 */
typedef enum {
    MPU6050_ACCEL_FS_2G = 0,   ///< +/- 2g
    MPU6050_ACCEL_FS_4G = 1,   ///< +/- 4g
    MPU6050_ACCEL_FS_8G = 2,   ///< +/- 8g
    MPU6050_ACCEL_FS_16G = 3   ///< +/- 16g
} mpu6050_accel_fs_t;

/**
 * @brief Digital Low Pass Filter (DLPF) bandwidth (CONFIG register, bits 2:0)
 */
typedef enum {
    MPU6050_DLPF_260HZ = 0,    ///< 260 Hz (Accel: 256 Hz, Gyro: 256 Hz)
    MPU6050_DLPF_184HZ = 1,    ///< 184 Hz (Accel: 188 Hz, Gyro: 188 Hz)
    MPU6050_DLPF_94HZ = 2,     ///< 94 Hz (Accel: 98 Hz, Gyro: 98 Hz)
    MPU6050_DLPF_44HZ = 3,     ///< 44 Hz (Accel: 42 Hz, Gyro: 42 Hz)
    MPU6050_DLPF_21HZ = 4,     ///< 21 Hz (Accel: 20 Hz, Gyro: 20 Hz)
    MPU6050_DLPF_10HZ = 5,     ///< 10 Hz (Accel: 10 Hz, Gyro: 10 Hz)
    MPU6050_DLPF_5HZ = 6       ///< 5 Hz (Accel: 5 Hz, Gyro: 5 Hz)
} mpu6050_dlpf_t;

/**
 * @brief Interrupt pin configuration (INT_PIN_CFG register)
 */
typedef struct {
    bool int_level;         ///< Interrupt pin level: false = active high, true = active low
    bool int_open;          ///< Interrupt pin mode: false = push-pull, true = open-drain
    bool latch_enable;      ///< Latch mode: false = pulse (50us), true = latch until cleared
    bool int_rd_clear;      ///< Interrupt status cleared on any read: false = no, true = yes
    bool fsync_int_level;   ///< FSYNC pin level: false = active high, true = active low
    bool fsync_int_mode;    ///< FSYNC pin mode: false = disabled, true = enabled
} mpu6050_int_pin_cfg_t;

/**
 * @brief Interrupt enable flags (INT_ENABLE register)
 */
typedef struct {
    bool data_ready;        ///< Data ready interrupt
    bool fifo_overflow;     ///< FIFO overflow interrupt
    bool motion_detect;     ///< Motion detection interrupt
} mpu6050_int_enable_t;

/**
 * @brief Raw sensor data structure
 */
typedef struct {
    int16_t accel_x;       ///< Raw accelerometer X-axis value
    int16_t accel_y;       ///< Raw accelerometer Y-axis value
    int16_t accel_z;       ///< Raw accelerometer Z-axis value
    int16_t temp;          ///< Raw temperature value
    int16_t gyro_x;        ///< Raw gyroscope X-axis value
    int16_t gyro_y;        ///< Raw gyroscope Y-axis value
    int16_t gyro_z;        ///< Raw gyroscope Z-axis value
} mpu6050_raw_data_t;

/**
 * @brief Physical sensor data structure (converted to units)
 */
typedef struct {
    float accel_x;         ///< Accelerometer X-axis in g
    float accel_y;         ///< Accelerometer Y-axis in g
    float accel_z;         ///< Accelerometer Z-axis in g
    float temp_c;          ///< Temperature in degrees Celsius
    float gyro_x;          ///< Gyroscope X-axis in degrees per second
    float gyro_y;          ///< Gyroscope Y-axis in degrees per second
    float gyro_z;          ///< Gyroscope Z-axis in degrees per second
} mpu6050_data_t;

/**
 * @brief FIFO enable configuration (FIFO_EN register)
 */
typedef struct {
    bool temp_fifo_en;     ///< Temperature data to FIFO
    bool xg_fifo_en;       ///< Gyro X-axis data to FIFO
    bool yg_fifo_en;       ///< Gyro Y-axis data to FIFO
    bool zg_fifo_en;       ///< Gyro Z-axis data to FIFO
    bool accel_fifo_en;    ///< Accelerometer data to FIFO
} mpu6050_fifo_enable_t;

/**
 * @brief Default configuration for MPU6050
 */
#define MPU6050_DEFAULT_CONFIG() { \
    .i2c_port = I2C_NUM_0, \
    .i2c_address = 0x70, \
    .sda_pin = GPIO_NUM_21, \
    .scl_pin = GPIO_NUM_22, \
    .i2c_freq_hz = 400000, \
    .enable_internal_pullup = true \
}

/* ============================================================================
 * Initialization & Setup Functions
 * ============================================================================ */

/**
 * @brief Initialize MPU6050 device
 * 
 * Initializes I2C bus, configures the device, wakes it from sleep mode,
 * and verifies connection by reading WHO_AM_I register (0x75).
 * 
 * @param config Pointer to configuration structure
 * @param handle Pointer to handle structure (will be populated on success)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_init(const mpu6050_config_t *config, mpu6050_handle_t *handle);

/**
 * @brief Deinitialize MPU6050 device
 * 
 * Removes device from I2C bus and deletes the bus handle.
 * 
 * @param handle Pointer to handle structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_deinit(mpu6050_handle_t *handle);

/**
 * @brief Set clock source
 * 
 * Configures the clock source in PWR_MGMT_1 register (0x6B, bits 2:0).
 * Recommended: MPU6050_CLK_PLL_X_GYRO
 * 
 * @param handle Pointer to handle structure
 * @param clk_src Clock source selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_clock_source(mpu6050_handle_t *handle, mpu6050_clock_source_t clk_src);

/**
 * @brief Set sample rate divider
 * 
 * Configures SMPLRT_DIV register (0x19). Sample rate = 1kHz / (1 + divider).
 * 
 * @param handle Pointer to handle structure
 * @param divider Sample rate divider (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_sample_rate_divider(mpu6050_handle_t *handle, uint8_t divider);

/* ============================================================================
 * Core Sensing & Conversion Functions
 * ============================================================================ */

/**
 * @brief Read raw sensor data (burst read)
 * 
 * Performs burst read of 14 bytes starting from ACCEL_XOUT_H register (0x3B).
 * Reads: Accel X/Y/Z (6 bytes), Temp (2 bytes), Gyro X/Y/Z (6 bytes).
 * 
 * @param handle Pointer to handle structure
 * @param data Pointer to raw data structure (will be populated)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_read_raw(mpu6050_handle_t *handle, mpu6050_raw_data_t *data);

/**
 * @brief Read and convert sensor data to physical units
 * 
 * Reads raw data and converts to g (accelerometer), deg/s (gyroscope), and deg C (temperature).
 * Conversion uses currently configured full scale ranges.
 * 
 * @param handle Pointer to handle structure
 * @param data Pointer to physical data structure (will be populated)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_read_data(mpu6050_handle_t *handle, mpu6050_data_t *data);

/**
 * @brief Convert raw accelerometer value to g
 * 
 * @param handle Pointer to handle structure
 * @param raw Raw accelerometer value
 * @return float Value in g
 */
float mpu6050_accel_to_g(mpu6050_handle_t *handle, int16_t raw);

/**
 * @brief Convert raw gyroscope value to degrees per second
 * 
 * @param handle Pointer to handle structure
 * @param raw Raw gyroscope value
 * @return float Value in degrees per second
 */
float mpu6050_gyro_to_dps(mpu6050_handle_t *handle, int16_t raw);

/**
 * @brief Convert raw temperature value to degrees Celsius
 * 
 * @param raw Raw temperature value
 * @return float Temperature in degrees Celsius
 */
float mpu6050_temp_to_celsius(int16_t raw);

/* ============================================================================
 * Configuration Functions (Setters/Getters)
 * ============================================================================ */

/**
 * @brief Set gyroscope full scale range
 * 
 * Configures GYRO_CONFIG register (0x1B, bits 4:3).
 * 
 * @param handle Pointer to handle structure
 * @param fs_range Full scale range selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_gyro_fs(mpu6050_handle_t *handle, mpu6050_gyro_fs_t fs_range);

/**
 * @brief Get current gyroscope full scale range
 * 
 * @param handle Pointer to handle structure
 * @param fs_range Pointer to store current full scale range
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_gyro_fs(mpu6050_handle_t *handle, mpu6050_gyro_fs_t *fs_range);

/**
 * @brief Set accelerometer full scale range
 * 
 * Configures ACCEL_CONFIG register (0x1C, bits 4:3).
 * 
 * @param handle Pointer to handle structure
 * @param fs_range Full scale range selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_accel_fs(mpu6050_handle_t *handle, mpu6050_accel_fs_t fs_range);

/**
 * @brief Get current accelerometer full scale range
 * 
 * @param handle Pointer to handle structure
 * @param fs_range Pointer to store current full scale range
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_accel_fs(mpu6050_handle_t *handle, mpu6050_accel_fs_t *fs_range);

/**
 * @brief Set Digital Low Pass Filter (DLPF) bandwidth
 * 
 * Configures CONFIG register (0x1A, bits 2:0).
 * 
 * @param handle Pointer to handle structure
 * @param dlpf DLPF bandwidth selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t dlpf);

/**
 * @brief Get current DLPF bandwidth
 * 
 * @param handle Pointer to handle structure
 * @param dlpf Pointer to store current DLPF setting
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t *dlpf);

/* ============================================================================
 * Power Management Functions
 * ============================================================================ */

/**
 * @brief Enable or disable sleep mode
 * 
 * Configures PWR_MGMT_1 register (0x6B, bit 6).
 * 
 * @param handle Pointer to handle structure
 * @param enable true to enable sleep mode, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_sleep_mode(mpu6050_handle_t *handle, bool enable);

/**
 * @brief Enable or disable cycle mode (low power accelerometer only)
 * 
 * Configures PWR_MGMT_1 register (0x6B, bit 5).
 * In cycle mode, device wakes up periodically to sample accelerometer.
 * 
 * @param handle Pointer to handle structure
 * @param enable true to enable cycle mode, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_cycle_mode(mpu6050_handle_t *handle, bool enable);

/**
 * @brief Enable or disable temperature sensor
 * 
 * Configures PWR_MGMT_1 register (0x6B, bit 3).
 * 
 * @param handle Pointer to handle structure
 * @param enable true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_temp_sensor(mpu6050_handle_t *handle, bool enable);

/* ============================================================================
 * FIFO Functions
 * ============================================================================ */

/**
 * @brief Enable or disable FIFO
 * 
 * Configures USER_CTRL register (0x6A, bit 6).
 * 
 * @param handle Pointer to handle structure
 * @param enable true to enable FIFO, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_fifo_enable(mpu6050_handle_t *handle, bool enable);

/**
 * @brief Reset FIFO
 * 
 * Sets FIFO reset bit in USER_CTRL register (0x6A, bit 2).
 * 
 * @param handle Pointer to handle structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_reset_fifo(mpu6050_handle_t *handle);

/**
 * @brief Get FIFO count
 * 
 * Reads FIFO_COUNTH (0x72) and FIFO_COUNTL (0x73) registers.
 * 
 * @param handle Pointer to handle structure
 * @param count Pointer to store FIFO count (0-1024)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_fifo_count(mpu6050_handle_t *handle, uint16_t *count);

/**
 * @brief Read bytes from FIFO
 * 
 * Reads data from FIFO_R_W register (0x74).
 * 
 * @param handle Pointer to handle structure
 * @param data Pointer to buffer to store FIFO data
 * @param len Number of bytes to read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_read_fifo(mpu6050_handle_t *handle, uint8_t *data, size_t len);

/**
 * @brief Configure which data goes into FIFO
 * 
 * Configures FIFO_EN register (0x23).
 * 
 * @param handle Pointer to handle structure
 * @param fifo_en Pointer to FIFO enable configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_fifo_enable_config(mpu6050_handle_t *handle, const mpu6050_fifo_enable_t *fifo_en);

/* ============================================================================
 * Interrupt Functions
 * ============================================================================ */

/**
 * @brief Configure interrupt pin behavior
 * 
 * Configures INT_PIN_CFG register (0x37).
 * 
 * @param handle Pointer to handle structure
 * @param cfg Pointer to interrupt pin configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_int_pin_cfg(mpu6050_handle_t *handle, const mpu6050_int_pin_cfg_t *cfg);

/**
 * @brief Enable or disable specific interrupts
 * 
 * Configures INT_ENABLE register (0x38).
 * 
 * @param handle Pointer to handle structure
 * @param int_en Pointer to interrupt enable configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_int_enable(mpu6050_handle_t *handle, const mpu6050_int_enable_t *int_en);

/**
 * @brief Read interrupt status
 * 
 * Reads INT_STATUS register (0x3A).
 * 
 * @param handle Pointer to handle structure
 * @param int_en Pointer to interrupt enable structure (will be populated with status)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_int_status(mpu6050_handle_t *handle, mpu6050_int_enable_t *int_en);

/* ============================================================================
 * Offset Calibration Functions
 * ============================================================================ */

/**
 * @brief Set accelerometer offsets
 * 
 * Writes to XA_OFFSET_H/L (0x06/0x07), YA_OFFSET_H/L (0x08/0x09), ZA_OFFSET_H/L (0x0A/0x0B).
 * Offsets are specified in physical units (g) and converted to LSB based on current full scale range.
 * 
 * @param handle Pointer to handle structure
 * @param x_offset X-axis offset in g
 * @param y_offset Y-axis offset in g
 * @param z_offset Z-axis offset in g
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_accel_offsets(mpu6050_handle_t *handle, float x_offset, float y_offset, float z_offset);

/**
 * @brief Get accelerometer offsets
 * 
 * Returns offsets in physical units (g) based on current full scale range.
 * 
 * @param handle Pointer to handle structure
 * @param x_offset Pointer to store X-axis offset in g
 * @param y_offset Pointer to store Y-axis offset in g
 * @param z_offset Pointer to store Z-axis offset in g
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_accel_offsets(mpu6050_handle_t *handle, float *x_offset, float *y_offset, float *z_offset);

/**
 * @brief Set gyroscope offsets
 * 
 * Writes to XG_OFFSET_H/L (0x13/0x14), YG_OFFSET_H/L (0x15/0x16), ZG_OFFSET_H/L (0x17/0x18).
 * Offsets are specified in physical units (deg/s) and converted to LSB based on current full scale range.
 * 
 * @param handle Pointer to handle structure
 * @param x_offset X-axis offset in deg/s
 * @param y_offset Y-axis offset in deg/s
 * @param z_offset Z-axis offset in deg/s
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_set_gyro_offsets(mpu6050_handle_t *handle, float x_offset, float y_offset, float z_offset);

/**
 * @brief Get gyroscope offsets
 * 
 * Returns offsets in physical units (degrees per second) based on current full scale range.
 * 
 * @param handle Pointer to handle structure
 * @param x_offset Pointer to store X-axis offset in deg/s
 * @param y_offset Pointer to store Y-axis offset in deg/s
 * @param z_offset Pointer to store Z-axis offset in deg/s
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mpu6050_get_gyro_offsets(mpu6050_handle_t *handle, float *x_offset, float *y_offset, float *z_offset);

/* ============================================================================
 * End of public API
 * ============================================================================ */

#ifdef __cplusplus
}
#endif

