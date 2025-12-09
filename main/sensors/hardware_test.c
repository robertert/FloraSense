/**
 * @file hardware_test.c
 * @brief Aplikacja testowa sprzętu robota ESP32
 * 
 * Testuje:
 * - Dual DC Motors (TB6612FNG Driver) - PWM i kierunek
 * - Water Pump (MOSFET) - GPIO kontrolowany
 * - Hall Sensor (A3144) - GPIO 18 z pull-up
 * - Microswitch (Docking) - GPIO 19 z pull-up
 * - IR Obstacle (KY-032) - GPIO 21
 */

#include "hardware_test.h"
#include "sensor_hall.h"
#include "sensor_dock.h"
#include "sensor_ir.h"
#include "esp_log.h"
#include "driver/gpio.h"
// #include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "HARDWARE_TEST";

// Konfiguracja GPIO dla silników
// #define MOTOR_LEFT_PWM_GPIO     25  // PWMA
// #define MOTOR_LEFT_AIN1_GPIO    26
// #define MOTOR_LEFT_AIN2_GPIO    27
// #define MOTOR_RIGHT_PWM_GPIO    14  // PWMB
// #define MOTOR_RIGHT_BIN1_GPIO   12
// #define MOTOR_RIGHT_BIN2_GPIO   13

// Konfiguracja GPIO dla pompy
#define PUMP_GPIO               33

// Konfiguracja GPIO dla czujników
// HALL_SENSOR_GPIO jest teraz obsługiwany przez sensor_hall.c
// DOCK_SWITCH_GPIO jest teraz obsługiwany przez sensor_dock.c
// IR_OBSTACLE_GPIO jest teraz obsługiwany przez sensor_ir.c

// Konfiguracja LEDC
// #define LEDC_TIMER              LEDC_TIMER_0
// #define LEDC_MODE               LEDC_LOW_SPEED_MODE
// #define LEDC_CHANNEL_LEFT       LEDC_CHANNEL_0
// #define LEDC_CHANNEL_RIGHT      LEDC_CHANNEL_1
// #define LEDC_DUTY_RES           LEDC_TIMER_8_BIT  // 8-bit resolution
// #define LEDC_FREQUENCY          5000              // 5000 Hz

// Struktura konfiguracji LEDC Timer
// static ledc_timer_config_t ledc_timer = {
//     .speed_mode       = LEDC_MODE,
//     .timer_num        = LEDC_TIMER,
//     .duty_resolution  = LEDC_DUTY_RES,
//     .freq_hz          = LEDC_FREQUENCY,
//     .clk_cfg          = LEDC_AUTO_CLK
// };

// Struktura konfiguracji LEDC Channel dla lewego silnika
// static ledc_channel_config_t ledc_channel_left = {
//     .speed_mode     = LEDC_MODE,
//     .channel        = LEDC_CHANNEL_LEFT,
//     .timer_sel      = LEDC_TIMER,
//     .intr_type      = LEDC_INTR_DISABLE,
//     .gpio_num       = MOTOR_LEFT_PWM_GPIO,
//     .duty           = 0,
//     .hpoint         = 0
// };

// Struktura konfiguracji LEDC Channel dla prawego silnika
// static ledc_channel_config_t ledc_channel_right = {
//     .speed_mode     = LEDC_MODE,
//     .channel        = LEDC_CHANNEL_RIGHT,
//     .timer_sel      = LEDC_TIMER,
//     .intr_type      = LEDC_INTR_DISABLE,
//     .gpio_num       = MOTOR_RIGHT_PWM_GPIO,
//     .duty           = 0,
//     .hpoint         = 0
// };

/**
 * @brief Konfiguruje GPIO dla silników i pompy jako wyjścia
 */
static void configure_motor_gpio(void)
{
    // Konfiguracja GPIO dla lewego silnika (kierunek)
    // gpio_config_t io_conf_left = {
    //     .intr_type    = GPIO_INTR_DISABLE,
    //     .mode         = GPIO_MODE_OUTPUT,
    //     .pin_bit_mask = (1ULL << MOTOR_LEFT_AIN1_GPIO) | (1ULL << MOTOR_LEFT_AIN2_GPIO),
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .pull_up_en   = GPIO_PULLUP_DISABLE,
    // };
    // gpio_config(&io_conf_left);

    // Konfiguracja GPIO dla prawego silnika (kierunek)
    // gpio_config_t io_conf_right = {
    //     .intr_type    = GPIO_INTR_DISABLE,
    //     .mode         = GPIO_MODE_OUTPUT,
    //     .pin_bit_mask = (1ULL << MOTOR_RIGHT_BIN1_GPIO) | (1ULL << MOTOR_RIGHT_BIN2_GPIO),
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .pull_up_en   = GPIO_PULLUP_DISABLE,
    // };
    // gpio_config(&io_conf_right);

    // Konfiguracja GPIO dla pompy
    gpio_config_t io_conf_pump = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PUMP_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf_pump);

    // Ustawienie początkowych wartości (wszystko wyłączone)
    // gpio_set_level(MOTOR_LEFT_AIN1_GPIO, 0);
    // gpio_set_level(MOTOR_LEFT_AIN2_GPIO, 0);
    // gpio_set_level(MOTOR_RIGHT_BIN1_GPIO, 0);
    // gpio_set_level(MOTOR_RIGHT_BIN2_GPIO, 0);
    gpio_set_level(PUMP_GPIO, 0);

    ESP_LOGI(TAG, "GPIO dla pompy skonfigurowane");
}

/**
 * @brief Konfiguruje GPIO dla czujników jako wejścia z pull-up gdzie wymagane
 */
static void configure_sensor_gpio(void)
{
    // Hall Sensor jest teraz obsługiwany przez sensor_hall.c
    // Inicjalizacja odbywa się w main przez sensor_hall_init()
    
    // Dock Switch jest teraz obsługiwany przez sensor_dock.c
    // Inicjalizacja odbywa się w main przez sensor_dock_init()
    
    // IR Obstacle jest teraz obsługiwany przez sensor_ir.c
    // Inicjalizacja odbywa się w main przez sensor_ir_init()

    ESP_LOGI(TAG, "GPIO dla czujników skonfigurowane (wszystkie czujniki są teraz w osobnych modułach)");
}

/**
 * @brief Konfiguruje LEDC dla kontroli PWM silników
 */
// static void configure_ledc(void)
// {
//     // Konfiguracja timera LEDC
//     esp_err_t ret = ledc_timer_config(&ledc_timer);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Błąd konfiguracji timera LEDC: %s", esp_err_to_name(ret));
//         return;
//     }

//     // Konfiguracja kanału dla lewego silnika
//     ret = ledc_channel_config(&ledc_channel_left);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Błąd konfiguracji kanału LEDC (lewy): %s", esp_err_to_name(ret));
//         return;
//     }

//     // Konfiguracja kanału dla prawego silnika
//     ret = ledc_channel_config(&ledc_channel_right);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Błąd konfiguracji kanału LEDC (prawy): %s", esp_err_to_name(ret));
//         return;
//     }

//     ESP_LOGI(TAG, "LEDC skonfigurowane: Timer=%d, Częstotliwość=%d Hz, Rozdzielczość=8-bit",
//              LEDC_TIMER, LEDC_FREQUENCY);
// }

/**
 * @brief Uruchamia oba silniki do przodu z 50% wypełnieniem PWM
 */
// static void motors_forward(void)
// {
//     // Ustawienie kierunku: AIN1=HIGH, AIN2=LOW (lewy silnik do przodu)
//     gpio_set_level(MOTOR_LEFT_AIN1_GPIO, 1);
//     gpio_set_level(MOTOR_LEFT_AIN2_GPIO, 0);

//     // Ustawienie kierunku: BIN1=HIGH, BIN2=LOW (prawy silnik do przodu)
//     gpio_set_level(MOTOR_RIGHT_BIN1_GPIO, 1);
//     gpio_set_level(MOTOR_RIGHT_BIN2_GPIO, 0);

//     // Ustawienie PWM na 50% (128 z 255 dla 8-bit)
//     uint32_t duty_50 = 128;  // 50% z 256 (8-bit: 0-255)
//     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LEFT, duty_50);
//     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_RIGHT, duty_50);
//     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LEFT);
//     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_RIGHT);

//     ESP_LOGI(TAG, "Silniki uruchomione do przodu (50%% PWM)");
// }

/**
 * @brief Zatrzymuje oba silniki
 */
// static void motors_stop(void)
// {
//     // Wyłączenie PWM (duty = 0)
//     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LEFT, 0);
//     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_RIGHT, 0);
//     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LEFT);
//     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_RIGHT);

//     // Ustawienie pinów kierunku na LOW
//     gpio_set_level(MOTOR_LEFT_AIN1_GPIO, 0);
//     gpio_set_level(MOTOR_LEFT_AIN2_GPIO, 0);
//     gpio_set_level(MOTOR_RIGHT_BIN1_GPIO, 0);
//     gpio_set_level(MOTOR_RIGHT_BIN2_GPIO, 0);

//     ESP_LOGI(TAG, "Silniki zatrzymane");
// }

/**
 * @brief Aktywuje pompę na 1000ms
 */
static void pump_activate(void)
{
    gpio_set_level(PUMP_GPIO, 1);
    ESP_LOGI(TAG, "Pompa włączona");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    gpio_set_level(PUMP_GPIO, 0);
    ESP_LOGI(TAG, "Pompa wyłączona");
}

/**
 * @brief Odczytuje stan wszystkich czujników
 * @param hall_state Wskaźnik do zmiennej na stan czujnika Hall
 * @param dock_state Wskaźnik do zmiennej na stan microswitch
 * @param ir_state Wskaźnik do zmiennej na stan czujnika IR
 */
static void read_sensors(int *hall_state, int *dock_state, int *ir_state)
{
    // Odczyt czujnika Hall przez moduł sensor_hall
    if (sensor_hall_is_initialized()) {
        *hall_state = sensor_hall_read();
    } else {
        *hall_state = -1;  // Nie zainicjalizowany
    }
    
    // Odczyt czujnika dock przez moduł sensor_dock
    if (sensor_dock_is_initialized()) {
        *dock_state = sensor_dock_read();
    } else {
        *dock_state = -1;  // Nie zainicjalizowany
    }
    
    // Odczyt czujnika IR przez moduł sensor_ir
    if (sensor_ir_is_initialized()) {
        *ir_state = sensor_ir_read();
    } else {
        *ir_state = -1;  // Nie zainicjalizowany
    }
}

/**
 * @brief Zadanie kontroli przez UART
 * 
 * Nasłuchuje pojedynczych znaków z stdin:
 * - 'w': Uruchom silniki do przodu (50% PWM)
 * - 's': Zatrzymaj silniki
 * - 'p': Aktywuj pompę na 1000ms
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Zadanie kontroli UART uruchomione");
    ESP_LOGI(TAG, "Dostępne komendy:");
    // ESP_LOGI(TAG, "  'w' - Uruchom silniki do przodu (50%%)");
    // ESP_LOGI(TAG, "  's' - Zatrzymaj silniki");
    ESP_LOGI(TAG, "  'p' - Aktywuj pompę (1000ms)");

    char c;
    while (1) {
        // Odczyt pojedynczego znaku (blokujący)
        c = fgetc(stdin);
        
        switch (c) {
            // case 'w':
            // case 'W':
            //     motors_forward();
            //     break;
                
            // case 's':
            // case 'S':
            //     motors_stop();
            //     break;
                
            case 'p':
            case 'P':
                pump_activate();
                break;
                
            case '\n':
            case '\r':
                // Ignoruj znaki nowej linii
                break;
                
            default:
                if (c != EOF) {
                    ESP_LOGW(TAG, "Nieznana komenda: '%c' (0x%02x)", c, (unsigned char)c);
                }
                break;
        }
    }
}

/**
 * @brief Zadanie monitorowania czujników
 * 
 * Odczytuje stan wszystkich czujników co 500ms i wyświetla je przez ESP_LOGI
 */
static void monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Zadanie monitorowania czujników uruchomione");

    int hall_state, dock_state, ir_state;

    while (1) {
        read_sensors(&hall_state, &dock_state, &ir_state);
        
        ESP_LOGI(TAG, "HALL: %d | DOCK: %d | IR: %d", 
                 hall_state, dock_state, ir_state);
        
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

esp_err_t hardware_test_init(void)
{
    ESP_LOGI(TAG, "Inicjalizacja aplikacji testowej sprzętu...");

    // Konfiguracja GPIO dla silników i pompy
    configure_motor_gpio();

    // Konfiguracja GPIO dla czujników
    configure_sensor_gpio();

    // Konfiguracja LEDC dla PWM
    // configure_ledc();

    // Utworzenie zadania kontroli UART
    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);

    // Utworzenie zadania monitorowania czujników
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Aplikacja testowa sprzętu zainicjalizowana pomyślnie");
    ESP_LOGI(TAG, "Użyj komend UART do testowania: 'p'");

    return ESP_OK;
}

