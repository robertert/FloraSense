/**
 * @file mpu6050.c
 * @brief ESP-IDF driver library implementation for MPU6050 6-axis MotionTracking device
 */

#include "mpu6050.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "MPU6050";

/* ============================================================================
 * Register Definitions
 * ============================================================================ */

// Configuration registers
#define MPU6050_REG_SMPLRT_DIV      0x19    ///< Sample Rate Divider
#define MPU6050_REG_CONFIG          0x1A    ///< Digital Low Pass Filter configuration
#define MPU6050_REG_GYRO_CONFIG     0x1B    ///< Gyroscope configuration (full scale range)
#define MPU6050_REG_ACCEL_CONFIG    0x1C    ///< Accelerometer configuration (full scale range)
#define MPU6050_REG_ACCEL_CONFIG2   0x1D    ///< Additional accelerometer configuration

// Offset registers (Accelerometer)
#define MPU6050_REG_XA_OFFSET_H     0x06    ///< Accelerometer X-axis offset high byte
#define MPU6050_REG_XA_OFFSET_L     0x07    ///< Accelerometer X-axis offset low byte
#define MPU6050_REG_YA_OFFSET_H     0x08    ///< Accelerometer Y-axis offset high byte
#define MPU6050_REG_YA_OFFSET_L     0x09    ///< Accelerometer Y-axis offset low byte
#define MPU6050_REG_ZA_OFFSET_H     0x0A    ///< Accelerometer Z-axis offset high byte
#define MPU6050_REG_ZA_OFFSET_L     0x0B    ///< Accelerometer Z-axis offset low byte

// Offset registers (Gyroscope)
#define MPU6050_REG_XG_OFFSET_H     0x13    ///< Gyroscope X-axis offset high byte
#define MPU6050_REG_XG_OFFSET_L     0x14    ///< Gyroscope X-axis offset low byte
#define MPU6050_REG_YG_OFFSET_H     0x15    ///< Gyroscope Y-axis offset high byte
#define MPU6050_REG_YG_OFFSET_L     0x16    ///< Gyroscope Y-axis offset low byte
#define MPU6050_REG_ZG_OFFSET_H     0x17    ///< Gyroscope Z-axis offset high byte
#define MPU6050_REG_ZG_OFFSET_L     0x18    ///< Gyroscope Z-axis offset low byte

// FIFO registers
#define MPU6050_REG_FIFO_EN         0x23    ///< FIFO enable register
#define MPU6050_REG_FIFO_COUNTH     0x72    ///< FIFO count high byte
#define MPU6050_REG_FIFO_COUNTL     0x73    ///< FIFO count low byte
#define MPU6050_REG_FIFO_R_W       0x74    ///< FIFO read/write register

// Interrupt registers
#define MPU6050_REG_INT_PIN_CFG     0x37    ///< Interrupt pin configuration
#define MPU6050_REG_INT_ENABLE      0x38    ///< Interrupt enable register
#define MPU6050_REG_INT_STATUS      0x3A    ///< Interrupt status register

// Sensor data registers (burst read starts here)
#define MPU6050_REG_ACCEL_XOUT_H    0x3B    ///< Accelerometer X-axis high byte (burst read start)
#define MPU6050_REG_ACCEL_XOUT_L    0x3C    ///< Accelerometer X-axis low byte
#define MPU6050_REG_ACCEL_YOUT_H    0x3D    ///< Accelerometer Y-axis high byte
#define MPU6050_REG_ACCEL_YOUT_L    0x3E    ///< Accelerometer Y-axis low byte
#define MPU6050_REG_ACCEL_ZOUT_H    0x3F    ///< Accelerometer Z-axis high byte
#define MPU6050_REG_ACCEL_ZOUT_L    0x40    ///< Accelerometer Z-axis low byte
#define MPU6050_REG_TEMP_OUT_H      0x41    ///< Temperature high byte
#define MPU6050_REG_TEMP_OUT_L      0x42    ///< Temperature low byte
#define MPU6050_REG_GYRO_XOUT_H     0x43    ///< Gyroscope X-axis high byte
#define MPU6050_REG_GYRO_XOUT_L     0x44    ///< Gyroscope X-axis low byte
#define MPU6050_REG_GYRO_YOUT_H     0x45    ///< Gyroscope Y-axis high byte
#define MPU6050_REG_GYRO_YOUT_L     0x46    ///< Gyroscope Y-axis low byte
#define MPU6050_REG_GYRO_ZOUT_H     0x47    ///< Gyroscope Z-axis high byte
#define MPU6050_REG_GYRO_ZOUT_L     0x48    ///< Gyroscope Z-axis low byte

// Power management registers
#define MPU6050_REG_PWR_MGMT_1      0x6B    ///< Power management 1 (clock source, sleep, cycle mode, temp disable)
#define MPU6050_REG_PWR_MGMT_2      0x6C    ///< Power management 2 (sensor enable/disable)

// User control register
#define MPU6050_REG_USER_CTRL       0x6A    ///< User control (FIFO enable, FIFO reset, etc.)

// Identification register
#define MPU6050_REG_WHO_AM_I        0x75    ///< Device identification register (should return 0x68 or 0x70)

// Register bit masks
#define MPU6050_PWR1_SLEEP_BIT      6       ///< Sleep mode bit in PWR_MGMT_1
#define MPU6050_PWR1_CYCLE_BIT      5       ///< Cycle mode bit in PWR_MGMT_1
#define MPU6050_PWR1_TEMP_DIS_BIT   3       ///< Temperature sensor disable bit in PWR_MGMT_1
#define MPU6050_PWR1_CLKSEL_MASK    0x07    ///< Clock source mask in PWR_MGMT_1

#define MPU6050_GYRO_FS_MASK        0x18    ///< Gyroscope full scale mask in GYRO_CONFIG (bits 4:3)
#define MPU6050_ACCEL_FS_MASK       0x18    ///< Accelerometer full scale mask in ACCEL_CONFIG (bits 4:3)
#define MPU6050_DLPF_MASK           0x07    ///< DLPF mask in CONFIG (bits 2:0)

#define MPU6050_USER_CTRL_FIFO_EN_BIT  6   ///< FIFO enable bit in USER_CTRL
#define MPU6050_USER_CTRL_FIFO_RST_BIT 2   ///< FIFO reset bit in USER_CTRL

#define MPU6050_INT_PIN_CFG_INT_LEVEL_BIT    7   ///< Interrupt level bit in INT_PIN_CFG
#define MPU6050_INT_PIN_CFG_INT_OPEN_BIT     6   ///< Interrupt open-drain bit in INT_PIN_CFG
#define MPU6050_INT_PIN_CFG_LATCH_EN_BIT     5   ///< Latch enable bit in INT_PIN_CFG
#define MPU6050_INT_PIN_CFG_INT_RD_CLEAR_BIT 4   ///< Interrupt read clear bit in INT_PIN_CFG
#define MPU6050_INT_PIN_CFG_FSYNC_INT_LEVEL_BIT  3   ///< FSYNC interrupt level bit in INT_PIN_CFG
#define MPU6050_INT_PIN_CFG_FSYNC_INT_MODE_BIT   2   ///< FSYNC interrupt mode bit in INT_PIN_CFG

#define MPU6050_INT_EN_DATA_RDY_BIT    0   ///< Data ready interrupt bit in INT_ENABLE
#define MPU6050_INT_EN_FIFO_OFLOW_BIT  4   ///< FIFO overflow interrupt bit in INT_ENABLE
#define MPU6050_INT_EN_MOTION_BIT      6   ///< Motion detection interrupt bit in INT_ENABLE

// Expected WHO_AM_I values
#define MPU6050_WHO_AM_I_VALUE_70    0x70   ///< Alternative WHO_AM_I value (some modules)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Write a single byte to a register
 * @note This function can be used during initialization (before handle->initialized is set)
 */
static esp_err_t mpu6050_write_register(mpu6050_handle_t *handle, uint8_t reg, uint8_t value)
{
    if (handle == NULL || handle->dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[] = {reg, value};
    esp_err_t ret = i2c_master_transmit(handle->dev_handle, data, sizeof(data), 1000 / portTICK_PERIOD_MS);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Read a single byte from a register
 * @note This function can be used during initialization (before handle->initialized is set)
 */
static esp_err_t mpu6050_read_register(mpu6050_handle_t *handle, uint8_t reg, uint8_t *value)
{
    if (handle == NULL || handle->dev_handle == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2c_master_transmit_receive(handle->dev_handle, &reg, 1, value, 1, 1000 / portTICK_PERIOD_MS);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Read multiple bytes starting from a register
 * @note This function can be used during initialization (before handle->initialized is set)
 */
static esp_err_t mpu6050_read_registers(mpu6050_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || handle->dev_handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2c_master_transmit_receive(handle->dev_handle, &reg, 1, data, len, 1000 / portTICK_PERIOD_MS);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read registers starting from 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Get current gyroscope full scale range (for conversion)
 */
static mpu6050_gyro_fs_t mpu6050_get_gyro_fs_internal(mpu6050_handle_t *handle)
{
    uint8_t reg_val;
    if (mpu6050_read_register(handle, MPU6050_REG_GYRO_CONFIG, &reg_val) != ESP_OK) {
        return MPU6050_GYRO_FS_250DPS; // Default
    }
    return (mpu6050_gyro_fs_t)((reg_val & MPU6050_GYRO_FS_MASK) >> 3);
}

/**
 * @brief Get current accelerometer full scale range (for conversion)
 */
static mpu6050_accel_fs_t mpu6050_get_accel_fs_internal(mpu6050_handle_t *handle)
{
    uint8_t reg_val;
    if (mpu6050_read_register(handle, MPU6050_REG_ACCEL_CONFIG, &reg_val) != ESP_OK) {
        return MPU6050_ACCEL_FS_2G; // Default
    }
    return (mpu6050_accel_fs_t)((reg_val & MPU6050_ACCEL_FS_MASK) >> 3);
}

/* ============================================================================
 * Initialization & Setup Functions
 * ============================================================================ */

esp_err_t mpu6050_init(const mpu6050_config_t *config, mpu6050_handle_t *handle)
{
    if (config == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    uint8_t who_am_i;

    // Initialize handle
    memset(handle, 0, sizeof(mpu6050_handle_t));
    handle->i2c_address = config->i2c_address;

    // Configure I2C bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = config->i2c_port,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = config->enable_internal_pullup,
    };

    ret = i2c_new_master_bus(&bus_config, &handle->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure I2C device
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = config->i2c_freq_hz,
    };

    ret = i2c_master_bus_add_device(handle->bus_handle, &dev_config, &handle->dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MPU6050 device to bus: %s", esp_err_to_name(ret));
        i2c_del_master_bus(handle->bus_handle);
        handle->bus_handle = NULL;
        return ret;
    }

    // Wait for device to stabilize
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Wake up device (clear sleep bit in PWR_MGMT_1)
    ret = mpu6050_write_register(handle, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up MPU6050");
        goto cleanup;
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Verify connection by reading WHO_AM_I
    ret = mpu6050_read_register(handle, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register");
        goto cleanup;
    }

    ESP_LOGI(TAG, "WHO_AM_I = 0x%02X (expected: 0x70)", who_am_i);
    
    if (who_am_i != MPU6050_WHO_AM_I_VALUE_70) {
        ESP_LOGW(TAG, "Unexpected WHO_AM_I value. Device may not be MPU6050 or connection issue.");
    }

    handle->initialized = true;
    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    
    return ESP_OK;

cleanup:
    i2c_master_bus_rm_device(handle->dev_handle);
    i2c_del_master_bus(handle->bus_handle);
    handle->dev_handle = NULL;
    handle->bus_handle = NULL;
    return ret;
}

esp_err_t mpu6050_deinit(mpu6050_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (handle->dev_handle != NULL) {
        ret = i2c_master_bus_rm_device(handle->dev_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove device from bus: %s", esp_err_to_name(ret));
        }
    }

    if (handle->bus_handle != NULL) {
        ret = i2c_del_master_bus(handle->bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete I2C bus: %s", esp_err_to_name(ret));
        }
    }

    memset(handle, 0, sizeof(mpu6050_handle_t));
    ESP_LOGI(TAG, "MPU6050 deinitialized");
    
    return ret;
}



/* ============================================================================
 * Core Sensing & Conversion Functions
 * ============================================================================ */

esp_err_t mpu6050_read_raw(mpu6050_handle_t *handle, mpu6050_raw_data_t *data)
{
    if (handle == NULL || !handle->initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw_data[14];
    esp_err_t ret = mpu6050_read_registers(handle, MPU6050_REG_ACCEL_XOUT_H, raw_data, 14);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse data (big-endian format)
    data->accel_x = (int16_t)((raw_data[0] << 8) | raw_data[1]);
    data->accel_y = (int16_t)((raw_data[2] << 8) | raw_data[3]);
    data->accel_z = (int16_t)((raw_data[4] << 8) | raw_data[5]);
    data->temp = (int16_t)((raw_data[6] << 8) | raw_data[7]);
    data->gyro_x = (int16_t)((raw_data[8] << 8) | raw_data[9]);
    data->gyro_y = (int16_t)((raw_data[10] << 8) | raw_data[11]);
    data->gyro_z = (int16_t)((raw_data[12] << 8) | raw_data[13]);

    return ESP_OK;
}

esp_err_t mpu6050_read_data(mpu6050_handle_t *handle, mpu6050_data_t *data)
{
    if (handle == NULL || !handle->initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mpu6050_raw_data_t raw;
    esp_err_t ret = mpu6050_read_raw(handle, &raw);
    if (ret != ESP_OK) {
        return ret;
    }

    // Convert to physical units
    data->accel_x = mpu6050_accel_to_g(handle, raw.accel_x);
    data->accel_y = mpu6050_accel_to_g(handle, raw.accel_y);
    data->accel_z = mpu6050_accel_to_g(handle, raw.accel_z);
    data->temp_c = mpu6050_temp_to_celsius(raw.temp);
    data->gyro_x = mpu6050_gyro_to_dps(handle, raw.gyro_x);
    data->gyro_y = mpu6050_gyro_to_dps(handle, raw.gyro_y);
    data->gyro_z = mpu6050_gyro_to_dps(handle, raw.gyro_z);

    return ESP_OK;
}

float mpu6050_accel_to_g(mpu6050_handle_t *handle, int16_t raw)
{
    mpu6050_accel_fs_t fs = mpu6050_get_accel_fs_internal(handle);
    float sensitivity;

    switch (fs) {
        case MPU6050_ACCEL_FS_2G:
            sensitivity = 16384.0f;  // LSB/g
            break;
        case MPU6050_ACCEL_FS_4G:
            sensitivity = 8192.0f;
            break;
        case MPU6050_ACCEL_FS_8G:
            sensitivity = 4096.0f;
            break;
        case MPU6050_ACCEL_FS_16G:
            sensitivity = 2048.0f;
            break;
        default:
            sensitivity = 16384.0f;
            break;
    }

    return (float)raw / sensitivity;
}

float mpu6050_gyro_to_dps(mpu6050_handle_t *handle, int16_t raw)
{
    mpu6050_gyro_fs_t fs = mpu6050_get_gyro_fs_internal(handle);
    float sensitivity;

    switch (fs) {
        case MPU6050_GYRO_FS_250DPS:
            sensitivity = 131.0f;  // LSB/(deg/s)
            break;
        case MPU6050_GYRO_FS_500DPS:
            sensitivity = 65.5f;
            break;
        case MPU6050_GYRO_FS_1000DPS:
            sensitivity = 32.8f;
            break;
        case MPU6050_GYRO_FS_2000DPS:
            sensitivity = 16.4f;
            break;
        default:
            sensitivity = 131.0f;
            break;
    }

    return (float)raw / sensitivity;
}

float mpu6050_temp_to_celsius(int16_t raw)
{
    // Temperature conversion: T(°C) = (TEMP_OUT / 340) + 36.53
    return ((float)raw / 340.0f) + 36.53f;
}

/* ============================================================================
 * Configuration Functions (Setters/Getters)
 * ============================================================================ */

esp_err_t mpu6050_set_gyro_fs(mpu6050_handle_t *handle, mpu6050_gyro_fs_t fs_range)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_GYRO_CONFIG, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear FS bits (4:3) and set new value
    reg_val = (reg_val & ~MPU6050_GYRO_FS_MASK) | ((fs_range << 3) & MPU6050_GYRO_FS_MASK);
    
    return mpu6050_write_register(handle, MPU6050_REG_GYRO_CONFIG, reg_val);
}

esp_err_t mpu6050_get_gyro_fs(mpu6050_handle_t *handle, mpu6050_gyro_fs_t *fs_range)
{
    if (handle == NULL || !handle->initialized || fs_range == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_GYRO_CONFIG, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    *fs_range = (mpu6050_gyro_fs_t)((reg_val & MPU6050_GYRO_FS_MASK) >> 3);
    return ESP_OK;
}

esp_err_t mpu6050_set_accel_fs(mpu6050_handle_t *handle, mpu6050_accel_fs_t fs_range)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_ACCEL_CONFIG, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear FS bits (4:3) and set new value
    reg_val = (reg_val & ~MPU6050_ACCEL_FS_MASK) | ((fs_range << 3) & MPU6050_ACCEL_FS_MASK);
    
    return mpu6050_write_register(handle, MPU6050_REG_ACCEL_CONFIG, reg_val);
}

esp_err_t mpu6050_get_accel_fs(mpu6050_handle_t *handle, mpu6050_accel_fs_t *fs_range)
{
    if (handle == NULL || !handle->initialized || fs_range == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_ACCEL_CONFIG, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    *fs_range = (mpu6050_accel_fs_t)((reg_val & MPU6050_ACCEL_FS_MASK) >> 3);
    return ESP_OK;
}

esp_err_t mpu6050_set_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t dlpf)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_CONFIG, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear DLPF bits (2:0) and set new value
    reg_val = (reg_val & ~MPU6050_DLPF_MASK) | (dlpf & MPU6050_DLPF_MASK);
    
    return mpu6050_write_register(handle, MPU6050_REG_CONFIG, reg_val);
}

esp_err_t mpu6050_get_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t *dlpf)
{
    if (handle == NULL || !handle->initialized || dlpf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_CONFIG, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    *dlpf = (mpu6050_dlpf_t)(reg_val & MPU6050_DLPF_MASK);
    return ESP_OK;
}

/* ============================================================================
 * Power Management Functions
 * ============================================================================ */

esp_err_t mpu6050_set_sleep_mode(mpu6050_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_PWR_MGMT_1, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        reg_val |= (1 << MPU6050_PWR1_SLEEP_BIT);
    } else {
        reg_val &= ~(1 << MPU6050_PWR1_SLEEP_BIT);
    }

    return mpu6050_write_register(handle, MPU6050_REG_PWR_MGMT_1, reg_val);
}

esp_err_t mpu6050_set_cycle_mode(mpu6050_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_PWR_MGMT_1, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        reg_val |= (1 << MPU6050_PWR1_CYCLE_BIT);
    } else {
        reg_val &= ~(1 << MPU6050_PWR1_CYCLE_BIT);
    }

    return mpu6050_write_register(handle, MPU6050_REG_PWR_MGMT_1, reg_val);
}

esp_err_t mpu6050_set_temp_sensor(mpu6050_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_PWR_MGMT_1, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        reg_val &= ~(1 << MPU6050_PWR1_TEMP_DIS_BIT);  // Clear bit to enable
    } else {
        reg_val |= (1 << MPU6050_PWR1_TEMP_DIS_BIT);   // Set bit to disable
    }

    return mpu6050_write_register(handle, MPU6050_REG_PWR_MGMT_1, reg_val);
}

/* ============================================================================
 * FIFO Functions
 * ============================================================================ */

esp_err_t mpu6050_set_fifo_enable(mpu6050_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_USER_CTRL, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        reg_val |= (1 << MPU6050_USER_CTRL_FIFO_EN_BIT);
    } else {
        reg_val &= ~(1 << MPU6050_USER_CTRL_FIFO_EN_BIT);
    }

    return mpu6050_write_register(handle, MPU6050_REG_USER_CTRL, reg_val);
}

esp_err_t mpu6050_reset_fifo(mpu6050_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_USER_CTRL, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    // Set FIFO reset bit
    reg_val |= (1 << MPU6050_USER_CTRL_FIFO_RST_BIT);
    ret = mpu6050_write_register(handle, MPU6050_REG_USER_CTRL, reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear FIFO reset bit (auto-clears, but we'll do it explicitly)
    vTaskDelay(1 / portTICK_PERIOD_MS);
    reg_val &= ~(1 << MPU6050_USER_CTRL_FIFO_RST_BIT);
    
    return mpu6050_write_register(handle, MPU6050_REG_USER_CTRL, reg_val);
}

esp_err_t mpu6050_get_fifo_count(mpu6050_handle_t *handle, uint16_t *count)
{
    if (handle == NULL || !handle->initialized || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2];
    esp_err_t ret = mpu6050_read_registers(handle, MPU6050_REG_FIFO_COUNTH, data, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    *count = (uint16_t)((data[0] << 8) | data[1]);
    return ESP_OK;
}

esp_err_t mpu6050_read_fifo(mpu6050_handle_t *handle, uint8_t *data, size_t len)
{
    if (handle == NULL || !handle->initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // FIFO read automatically increments from FIFO_R_W register (0x74)
    return mpu6050_read_registers(handle, MPU6050_REG_FIFO_R_W, data, len);
}

esp_err_t mpu6050_set_fifo_enable_config(mpu6050_handle_t *handle, const mpu6050_fifo_enable_t *fifo_en)
{
    if (handle == NULL || !handle->initialized || fifo_en == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val = 0;

    if (fifo_en->temp_fifo_en) {
        reg_val |= (1 << 7);
    }
    if (fifo_en->xg_fifo_en) {
        reg_val |= (1 << 6);
    }
    if (fifo_en->yg_fifo_en) {
        reg_val |= (1 << 5);
    }
    if (fifo_en->zg_fifo_en) {
        reg_val |= (1 << 4);
    }
    if (fifo_en->accel_fifo_en) {
        reg_val |= (1 << 3);
    }

    return mpu6050_write_register(handle, MPU6050_REG_FIFO_EN, reg_val);
}

/* ============================================================================
 * Interrupt Functions
 * ============================================================================ */

esp_err_t mpu6050_set_int_pin_cfg(mpu6050_handle_t *handle, const mpu6050_int_pin_cfg_t *cfg)
{
    if (handle == NULL || !handle->initialized || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val = 0;

    if (cfg->int_level) {
        reg_val |= (1 << MPU6050_INT_PIN_CFG_INT_LEVEL_BIT);
    }
    if (cfg->int_open) {
        reg_val |= (1 << MPU6050_INT_PIN_CFG_INT_OPEN_BIT);
    }
    if (cfg->latch_enable) {
        reg_val |= (1 << MPU6050_INT_PIN_CFG_LATCH_EN_BIT);
    }
    if (cfg->int_rd_clear) {
        reg_val |= (1 << MPU6050_INT_PIN_CFG_INT_RD_CLEAR_BIT);
    }
    if (cfg->fsync_int_level) {
        reg_val |= (1 << MPU6050_INT_PIN_CFG_FSYNC_INT_LEVEL_BIT);
    }
    if (cfg->fsync_int_mode) {
        reg_val |= (1 << MPU6050_INT_PIN_CFG_FSYNC_INT_MODE_BIT);
    }

    return mpu6050_write_register(handle, MPU6050_REG_INT_PIN_CFG, reg_val);
}

esp_err_t mpu6050_set_int_enable(mpu6050_handle_t *handle, const mpu6050_int_enable_t *int_en)
{
    if (handle == NULL || !handle->initialized || int_en == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val = 0;

    if (int_en->data_ready) {
        reg_val |= (1 << MPU6050_INT_EN_DATA_RDY_BIT);
    }
    if (int_en->fifo_overflow) {
        reg_val |= (1 << MPU6050_INT_EN_FIFO_OFLOW_BIT);
    }
    if (int_en->motion_detect) {
        reg_val |= (1 << MPU6050_INT_EN_MOTION_BIT);
    }

    return mpu6050_write_register(handle, MPU6050_REG_INT_ENABLE, reg_val);
}

esp_err_t mpu6050_get_int_status(mpu6050_handle_t *handle, mpu6050_int_enable_t *int_en)
{
    if (handle == NULL || !handle->initialized || int_en == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val;
    esp_err_t ret = mpu6050_read_register(handle, MPU6050_REG_INT_STATUS, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }

    int_en->data_ready = (reg_val & (1 << MPU6050_INT_EN_DATA_RDY_BIT)) != 0;
    int_en->fifo_overflow = (reg_val & (1 << MPU6050_INT_EN_FIFO_OFLOW_BIT)) != 0;
    int_en->motion_detect = (reg_val & (1 << MPU6050_INT_EN_MOTION_BIT)) != 0;

    return ESP_OK;
}

/* ============================================================================
 * Offset Calibration Functions
 * ============================================================================ */

esp_err_t mpu6050_set_accel_offsets(mpu6050_handle_t *handle, float x_offset, float y_offset, float z_offset)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get current full scale range to determine sensitivity
    mpu6050_accel_fs_t fs = mpu6050_get_accel_fs_internal(handle);
    float sensitivity;

    switch (fs) {
        case MPU6050_ACCEL_FS_2G:
            sensitivity = 16384.0f;  // LSB/g
            break;
        case MPU6050_ACCEL_FS_4G:
            sensitivity = 8192.0f;
            break;
        case MPU6050_ACCEL_FS_8G:
            sensitivity = 4096.0f;
            break;
        case MPU6050_ACCEL_FS_16G:
            sensitivity = 2048.0f;
            break;
        default:
            sensitivity = 16384.0f;
            break;
    }

    // Convert from physical units (g) to LSB
    int16_t x_offset_lsb = (int16_t)(x_offset * sensitivity);
    int16_t y_offset_lsb = (int16_t)(y_offset * sensitivity);
    int16_t z_offset_lsb = (int16_t)(z_offset * sensitivity);

    esp_err_t ret;

    // Write X-axis offset
    ret = mpu6050_write_register(handle, MPU6050_REG_XA_OFFSET_H, (uint8_t)(x_offset_lsb >> 8));
    if (ret != ESP_OK) return ret;
    ret = mpu6050_write_register(handle, MPU6050_REG_XA_OFFSET_L, (uint8_t)(x_offset_lsb & 0xFF));
    if (ret != ESP_OK) return ret;

    // Write Y-axis offset
    ret = mpu6050_write_register(handle, MPU6050_REG_YA_OFFSET_H, (uint8_t)(y_offset_lsb >> 8));
    if (ret != ESP_OK) return ret;
    ret = mpu6050_write_register(handle, MPU6050_REG_YA_OFFSET_L, (uint8_t)(y_offset_lsb & 0xFF));
    if (ret != ESP_OK) return ret;

    // Write Z-axis offset
    ret = mpu6050_write_register(handle, MPU6050_REG_ZA_OFFSET_H, (uint8_t)(z_offset_lsb >> 8));
    if (ret != ESP_OK) return ret;
    ret = mpu6050_write_register(handle, MPU6050_REG_ZA_OFFSET_L, (uint8_t)(z_offset_lsb & 0xFF));
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t mpu6050_get_accel_offsets(mpu6050_handle_t *handle, float *x_offset, float *y_offset, float *z_offset)
{
    if (handle == NULL || !handle->initialized || x_offset == NULL || y_offset == NULL || z_offset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[6];
    esp_err_t ret = mpu6050_read_registers(handle, MPU6050_REG_XA_OFFSET_H, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    // Read raw offset values (LSB)
    int16_t raw_x_offset = (int16_t)((data[0] << 8) | data[1]);
    int16_t raw_y_offset = (int16_t)((data[2] << 8) | data[3]);
    int16_t raw_z_offset = (int16_t)((data[4] << 8) | data[5]);

    // Get current full scale range to determine sensitivity
    mpu6050_accel_fs_t fs = mpu6050_get_accel_fs_internal(handle);
    float sensitivity;

    switch (fs) {
        case MPU6050_ACCEL_FS_2G:
            sensitivity = 16384.0f;  // LSB/g
            break;
        case MPU6050_ACCEL_FS_4G:
            sensitivity = 8192.0f;
            break;
        case MPU6050_ACCEL_FS_8G:
            sensitivity = 4096.0f;
            break;
        case MPU6050_ACCEL_FS_16G:
            sensitivity = 2048.0f;
            break;
        default:
            sensitivity = 16384.0f;
            break;
    }

    // Convert to physical units (g)
    *x_offset = (float)raw_x_offset / sensitivity;
    *y_offset = (float)raw_y_offset / sensitivity;
    *z_offset = (float)raw_z_offset / sensitivity;

    return ESP_OK;
}

esp_err_t mpu6050_set_gyro_offsets(mpu6050_handle_t *handle, float x_offset, float y_offset, float z_offset)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get current full scale range to determine sensitivity
    mpu6050_gyro_fs_t fs = mpu6050_get_gyro_fs_internal(handle);
    float sensitivity;

    switch (fs) {
        case MPU6050_GYRO_FS_250DPS:
            sensitivity = 65.5f;  // LSB/(deg/s)
            break;
        case MPU6050_GYRO_FS_500DPS:
            sensitivity = 32.8f;
            break;
        case MPU6050_GYRO_FS_1000DPS:
            sensitivity = 16.4f;
            break;
        case MPU6050_GYRO_FS_2000DPS:
            sensitivity = 8.2f;
            break;
        default:
            sensitivity = 65.5f;
            break;
    }

    // Convert from physical units (deg/s) to LSB
    int16_t x_offset_lsb = (int16_t)(x_offset * sensitivity);
    int16_t y_offset_lsb = (int16_t)(y_offset * sensitivity);
    int16_t z_offset_lsb = (int16_t)(z_offset * sensitivity);

    esp_err_t ret;

    // Write X-axis offset
    ret = mpu6050_write_register(handle, MPU6050_REG_XG_OFFSET_H, (uint8_t)(x_offset_lsb >> 8));
    if (ret != ESP_OK) return ret;
    ret = mpu6050_write_register(handle, MPU6050_REG_XG_OFFSET_L, (uint8_t)(x_offset_lsb & 0xFF));
    if (ret != ESP_OK) return ret;

    // Write Y-axis offset
    ret = mpu6050_write_register(handle, MPU6050_REG_YG_OFFSET_H, (uint8_t)(y_offset_lsb >> 8));
    if (ret != ESP_OK) return ret;
    ret = mpu6050_write_register(handle, MPU6050_REG_YG_OFFSET_L, (uint8_t)(y_offset_lsb & 0xFF));
    if (ret != ESP_OK) return ret;

    // Write Z-axis offset
    ret = mpu6050_write_register(handle, MPU6050_REG_ZG_OFFSET_H, (uint8_t)(z_offset_lsb >> 8));
    if (ret != ESP_OK) return ret;
    ret = mpu6050_write_register(handle, MPU6050_REG_ZG_OFFSET_L, (uint8_t)(z_offset_lsb & 0xFF));
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t mpu6050_get_gyro_offsets(mpu6050_handle_t *handle, float *x_offset, float *y_offset, float *z_offset)
{
    if (handle == NULL || !handle->initialized || x_offset == NULL || y_offset == NULL || z_offset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[6];
    esp_err_t ret = mpu6050_read_registers(handle, MPU6050_REG_XG_OFFSET_H, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    // Read raw offset values (LSB)
    int16_t raw_x_offset = (int16_t)((data[0] << 8) | data[1]);
    int16_t raw_y_offset = (int16_t)((data[2] << 8) | data[3]);
    int16_t raw_z_offset = (int16_t)((data[4] << 8) | data[5]);

    // Get current full scale range to determine sensitivity
    mpu6050_gyro_fs_t fs = mpu6050_get_gyro_fs_internal(handle);
    float sensitivity;

    switch (fs) {
        case MPU6050_GYRO_FS_250DPS:
            sensitivity = 65.5f;  // LSB/(deg/s)
            break;
        case MPU6050_GYRO_FS_500DPS:
            sensitivity = 32.8f;
            break;
        case MPU6050_GYRO_FS_1000DPS:
            sensitivity = 16.4f;
            break;
        case MPU6050_GYRO_FS_2000DPS:
            sensitivity = 8.2f;
            break;
        default:
            sensitivity = 65.5f;
            break;
    }

    // Convert to physical units (deg/s)
    *x_offset = (float)raw_x_offset / sensitivity;
    *y_offset = (float)raw_y_offset / sensitivity;
    *z_offset = (float)raw_z_offset / sensitivity;

    return ESP_OK;
}


