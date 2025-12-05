#include "bmp280.h"
#include "esp_log.h"
#include "esp_err.h"
#include <unistd.h>
#include "lwip/sys.h"

static const char *TAG = "BMP280";

// I2C handles
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

// Rejestry BMP280
#define BMP280_REG_TEMP_MSB     0xFA
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_CONFIG       0xF5
#define dig_T1_R                0x88
#define dig_T2_R                0x8A
#define dig_T3_R                0x8C

// Kalibracja
static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

// Inicjalizacja I2C i BMP280
void bmp280_init(void)
{
    // Inicjalizacja I2C
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = 21,
        .scl_io_num = 22,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMP280_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    // Konfiguracja sensora: oversampling 1x, normal mode
    uint8_t ctrl_meas = (1 << 5) | (1 << 2) | 3;  // osrs_t=1, osrs_p=1, mode=3
    uint8_t config_reg = (5 << 5) | (0 << 2) | 0; // t_sb=1000ms, filter off, 3-wire SPI disable

    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, (uint8_t[]){BMP280_REG_CTRL_MEAS, ctrl_meas}, 2, 1000 / portTICK_PERIOD_MS));
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, (uint8_t[]){BMP280_REG_CONFIG, config_reg}, 2, 1000 / portTICK_PERIOD_MS));

    // Odczyt kalibracji temperatury
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, (uint8_t[]){dig_T1_R}, 1, (uint8_t*)&dig_T1, sizeof(dig_T1), 1000 / portTICK_PERIOD_MS));
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, (uint8_t[]){dig_T2_R}, 1, (uint8_t*)&dig_T2, sizeof(dig_T2), 1000 / portTICK_PERIOD_MS));
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, (uint8_t[]){dig_T3_R}, 1, (uint8_t*)&dig_T3, sizeof(dig_T3), 1000 / portTICK_PERIOD_MS));

    ESP_LOGI(TAG, "BMP280 initialized: dig_T1=%d, dig_T2=%d, dig_T3=%d", dig_T1, dig_T2, dig_T3);
}

// Kompensacja temperatury
static double bmp280_compensate_T_double(int32_t adc_T)
{
    double var1 = (((double)adc_T)/16384.0 - ((double)dig_T1)/1024.0) * dig_T2;
    double var2 = ((((double)adc_T)/131072.0 - ((double)dig_T1)/8192.0) *
                   (((double)adc_T)/131072.0 - ((double)dig_T1)/8192.0)) * dig_T3;
    return (var1 + var2) / 5120.0;
}

// Odczyt temperatury
double bmp280_read_temperature(void)
{
    uint8_t buf[BMP280_TEMP_REG_SIZE];
    int32_t temp_raw;

    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, (uint8_t[]){BMP280_REG_TEMP_MSB}, 1, buf, BMP280_TEMP_REG_SIZE, 1000 / portTICK_PERIOD_MS));

    temp_raw = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
    return bmp280_compensate_T_double(temp_raw);
}