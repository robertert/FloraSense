#include "dock_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "dock_control";

static gpio_num_t hall_gpio_num = GPIO_NUM_NC;
static gpio_num_t pump_gpio_num = GPIO_NUM_NC;
static dock_control_hall_cb_t hall_state_cb = NULL;
static bool initialized = false;
// hall_state: 0 = doniczka OBECNA (czujnik przyciągnięty magnesem, wyjście w stanie niskim),
//             1 = doniczka BRAK (wejście podciągnięte do VCC przez pull-up)
static volatile uint8_t hall_state = 1;
static volatile bool pump_active = false;
static TimerHandle_t pump_timer = NULL;
static TaskHandle_t hall_monitor_task_handle = NULL;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static void set_pump(bool on)
{
    gpio_set_level(pump_gpio_num, on ? 1 : 0);
    pump_active = on;
}

static void pump_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    taskENTER_CRITICAL(&lock);
    set_pump(false);
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Pompa zatrzymana (timer)");
}

static void hall_monitor_task(void *param)
{
    (void)param;
    uint8_t last_state = hall_state;

    while (1) {
        uint8_t current = gpio_get_level(hall_gpio_num) ? 1 : 0;
        if (current != last_state) {
            last_state = current;
            hall_state = current;
            ESP_LOGI(TAG, "Hall zmiana stanu: %u", current);
            if (hall_state_cb) {
                hall_state_cb(current);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t dock_control_init(gpio_num_t hall_gpio, gpio_num_t pump_gpio, dock_control_hall_cb_t hall_cb)
{
    hall_gpio_num = hall_gpio;
    pump_gpio_num = pump_gpio;
    hall_state_cb = hall_cb;

    gpio_config_t hall_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << hall_gpio_num,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t ret = gpio_config(&hall_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Konfiguracja hall GPIO %d nieudana: %s", hall_gpio_num, esp_err_to_name(ret));
        return ret;
    }

    gpio_config_t pump_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << pump_gpio_num,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ret = gpio_config(&pump_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Konfiguracja pomp GPIO %d nieudana: %s", pump_gpio_num, esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(pump_gpio_num, 0);

    pump_timer = xTimerCreate("pump_off_timer", pdMS_TO_TICKS(1000), pdFALSE, NULL, pump_timer_cb);
    if (!pump_timer) {
        ESP_LOGE(TAG, "Nie udało się stworzyć timera pompy");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ok = xTaskCreate(hall_monitor_task, "hall_monitor_task", 2048, NULL, 5, &hall_monitor_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Nie udało się stworzyć taska hall monitor");
        return ESP_ERR_NO_MEM;
    }

    hall_state = gpio_get_level(hall_gpio_num) ? 1 : 0;
    if (hall_state_cb) {
        hall_state_cb(hall_state);
    }

    initialized = true;
    ESP_LOGI(TAG, "Dock control init OK (hall GPIO %d, pump GPIO %d)", hall_gpio_num, pump_gpio_num);
    return ESP_OK;
}

uint8_t dock_control_get_hall_state(void)
{
    return hall_state;
}

bool dock_control_is_pump_active(void)
{
    return pump_active;
}

esp_err_t dock_control_handle_water_command(uint32_t duration_ms)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t clamped = duration_ms;
    if (clamped < 100) {
        clamped = 100;
    }
    if (clamped > 600000) {
        clamped = 600000;
    }

    // 0 = doniczka obecna (magnes przy czujniku), 1 = brak doniczki
    if (hall_state != 0) {
        ESP_LOGW(TAG, "Polecenie water odrzucone: brak doniczki (Hall=%u)", hall_state);
        return ESP_ERR_INVALID_STATE;
    }

    if (pump_active) {
        ESP_LOGW(TAG, "Pompa już aktywna - ignoruję nowe polecenie");
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&lock);
    set_pump(true);
    taskEXIT_CRITICAL(&lock);

    ESP_LOGI(TAG, "Pompa START na %lums", (unsigned long)clamped);
    if (xTimerChangePeriod(pump_timer, pdMS_TO_TICKS(clamped), 0) != pdPASS ||
        xTimerStart(pump_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Nie udało się uruchomić timera pompy");
        dock_control_stop_pump();
        return ESP_FAIL;
    }

    return ESP_OK;
}

void dock_control_stop_pump(void)
{
    taskENTER_CRITICAL(&lock);
    set_pump(false);
    taskEXIT_CRITICAL(&lock);
}

