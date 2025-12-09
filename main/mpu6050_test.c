/**
 * @file mpu6050_test.c
 * @brief PEŁNA PREZENTACJA możliwości biblioteki MPU6050.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h" // Potrzebne do odczytu pinu INT
#include "mpu6050.h"

// --- Konfiguracja Pinów ---
#define I2C_SDA_PIN  GPIO_NUM_21
#define I2C_SCL_PIN  GPIO_NUM_22
#define MPU_INT_PIN  GPIO_NUM_19  // Dodatkowy pin do testu przerwań

static const char *TAG = "MPU_FULL_DEMO";

// Funkcja pomocnicza: Pauza i odliczanie z instrukcją
static void wait_and_explain(const char* message, int seconds)
{
    printf("\n");
    ESP_LOGW(TAG, ">>> INSTRUKCJA: %s <<<", message);
    
    // --- BLOKADA NA ENTER ---
    printf("Nacisnij [ENTER], aby rozpoczac...");
    fflush(stdout); // Wymuś wyświetlenie napisu natychmiast

    // Pętla czekająca na znak nowej linii (\n) lub powrotu karetki (\r)
    while(1) {
        int c = getchar();
        if (c == '\n' || c == '\r') {
            break; // Wyjdź z pętli, gdy wykryto Enter
        }
        // Małe opóźnienie, żeby nie katować procesora (Feed Watchdog)
        // w przypadku gdyby getchar() zwracał błąd/-1 w pętli
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
    printf("\n");
    // ------------------------

    for(int i = seconds; i > 0; i--) {
        printf("Start za: %d...\r", i);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("\n--- START ETAPU ---\n");
}

static void mpu6050_test_task(void *param)
{
    // ========================================================================
    // KROK 1: Inicjalizacja i Core Setup
    // ========================================================================
    ESP_LOGI(TAG, "1. INICJALIZACJA...");
    
    mpu6050_config_t conf = {
        .i2c_port = I2C_NUM_0,
        .i2c_address = 0x68,
        .sda_pin = I2C_SDA_PIN,
        .scl_pin = I2C_SCL_PIN,
        .i2c_freq_hz = 400000,
        .enable_internal_pullup = true
    };

    mpu6050_handle_t mpu;
    if (mpu6050_init(&conf, &mpu) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd init!"); vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "MPU6050 Init OK. WHO_AM_I zweryfikowane.");
    
    // 1. Konfiguracja pinu GPIO w ESP32
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MPU_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // 2. Konfiguracja pinu w MPU: Latch (zatrzask) = TRUE
    // Dzięki temu pin INT będzie świecił ciągle AŻ do momentu, gdy odczytamy dane.
    mpu6050_int_pin_cfg_t int_cfg = {
        .int_level = false,   // 0 = Active High (3.3V gdy przerwanie)
        .int_open = false,    // 0 = Push-Pull
        .latch_enable = true, // 1 = Trzymaj stan wysoki aż procesor przeczyta
        .int_rd_clear = true  // 1 = Kasuj stan wysoki przy odczycie
    };
    mpu6050_set_int_pin_cfg(&mpu, &int_cfg);

    // 3. Włączamy przerwanie DATA READY
    mpu6050_int_enable_t int_en = { .data_ready = true };
    mpu6050_set_int_enable(&mpu, &int_en);

    // ========================================================================
    // KROK 2: Offsets (Kalibracja) - Odczyt i Zapis
    // ========================================================================
    wait_and_explain("Odczyt rejestrów kalibracyjnych (Offsets)", 2);
    
    float ax_off, ay_off, az_off;
    float gx_off, gy_off, gz_off;

    // Pobieramy aktualne offsety (fabryczne lub poprzednie)
    mpu6050_get_accel_offsets(&mpu, &ax_off, &ay_off, &az_off);
    mpu6050_get_gyro_offsets(&mpu, &gx_off, &gy_off, &gz_off);

    ESP_LOGI(TAG, "Aktualne Offsety ACC: X=%.4f g, Y=%.4f g, Z=%.4f g", ax_off, ay_off, az_off);
    ESP_LOGI(TAG, "Aktualne Offsety GYRO: X=%.4f dps, Y=%.4f dps, Z=%.4f dps", gx_off, gy_off, gz_off);
    ESP_LOGI(TAG, "(Funkcje set_accel_offsets pozwalają je nadpisać, by wyzerować błąd)");

    // ========================================================================
    // KROK 3: Konfiguracja Zakresów i Weryfikacja (Getters)
    // ========================================================================
    wait_and_explain("Zmiana czułości i weryfikacja (Getters)", 2);

    // Ustawiamy
    mpu6050_set_accel_fs(&mpu, MPU6050_ACCEL_FS_16G);
    mpu6050_set_gyro_fs(&mpu, MPU6050_GYRO_FS_2000DPS);
    mpu6050_set_dlpf(&mpu, MPU6050_DLPF_184HZ);

    // Weryfikujemy czy się ustawiło (używając funkcji get_)
    mpu6050_accel_fs_t current_afs;
    mpu6050_gyro_fs_t current_gfs;
    mpu6050_dlpf_t current_dlpf;

    mpu6050_get_accel_fs(&mpu, &current_afs);
    mpu6050_get_gyro_fs(&mpu, &current_gfs);
    mpu6050_get_dlpf(&mpu, &current_dlpf);

    ESP_LOGI(TAG, "Weryfikacja Accel FS (oczekiwane 3 [16G]): %d", current_afs);
    ESP_LOGI(TAG, "Weryfikacja Gyro FS (oczekiwane 3 [2000dps]): %d", current_gfs);
    ESP_LOGI(TAG, "Weryfikacja DLPF (oczekiwane 1 [184Hz]): %d", current_dlpf);

    // ========================================================================
    // KROK 5: Prezentacja Danych (Core Sensing)
    // ========================================================================
    wait_and_explain("Pomiar danych: Poruszaj czujnikiem!", 3);
    
    // Wracamy do większej czułości dla lepszego efektu
    mpu6050_set_accel_fs(&mpu, MPU6050_ACCEL_FS_2G);
    mpu6050_set_gyro_fs(&mpu, MPU6050_GYRO_FS_500DPS);

    mpu6050_data_t data;
    for(int i=0; i<40; i++) {
        mpu6050_read_data(&mpu, &data);
        ESP_LOGI(TAG, "ACC[g]: X:%.2f Y:%.2f | GYRO[dps]: Z:%.2f | TEMP: %.1fC", 
                 data.accel_x, data.accel_y, data.gyro_z, data.temp_c);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    // Dodatkowe, osobne pomiary po 20s dla każdej wielkości (ACC, GYRO, TEMP)
    wait_and_explain("Akcelerometr: 20 sekund pomiaru", 1);
    for (int t = 0; t < 20; t++) {
        if (mpu6050_read_data(&mpu, &data) == ESP_OK) {
            ESP_LOGI(TAG, "[%02ds] ACC[g] X=%.3f Y=%.3f Z=%.3f",
                     t + 1, data.accel_x, data.accel_y, data.accel_z);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    wait_and_explain("Żyroskop: 20 sekund pomiaru", 1);
    for (int t = 0; t < 20; t++) {
        if (mpu6050_read_data(&mpu, &data) == ESP_OK) {
            ESP_LOGI(TAG, "[%02ds] GYRO[dps] X=%.2f Y=%.2f Z=%.2f",
                     t + 1, data.gyro_x, data.gyro_y, data.gyro_z);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    wait_and_explain("Temperatura: 20 sekund pomiaru", 1);
    for (int t = 0; t < 20; t++) {
        if (mpu6050_read_data(&mpu, &data) == ESP_OK) {
            ESP_LOGI(TAG, "[%02ds] TEMP[C] %.2f", t + 1, data.temp_c);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ========================================================================
    // KROK 6: Pokazanie ofsetów
    // ========================================================================

    wait_and_explain("Odczyt offsetów", 2);
    mpu6050_get_accel_offsets(&mpu, &ax_off, &ay_off, &az_off);
    mpu6050_get_gyro_offsets(&mpu, &gx_off, &gy_off, &gz_off);


    ESP_LOGI(TAG, "Aktualne Offsety ACC: X=%.4f g, Y=%.4f g, Z=%.4f g", ax_off, ay_off, az_off);
    ESP_LOGI(TAG, "Aktualne Offsety GYRO: X=%.4f dps, Y=%.4f dps, Z=%.4f dps", gx_off, gy_off, gz_off);

    ESP_LOGI(TAG, "Odczyt danych:");

    for(int i=0; i<5; i++) {
        mpu6050_read_data(&mpu, &data);
        ESP_LOGI(TAG, "ACC[g]: X:%.2f Y:%.2f Z:%.2f | GYRO[dps]: X:%.2f Y:%.2f Z:%.2f", 
                 data.accel_x, data.accel_y, data.accel_z, data.gyro_x, data.gyro_y, data.gyro_z);
        vTaskDelay(pdMS_TO_TICKS(300));
    }


    ESP_LOGI(TAG, "Zmiana offsetów ( 20 dps dla GYRO):");
    // Teraz funkcje set przyjmują wartości w jednostkach fizycznych - dużo prostsze!
    mpu6050_set_gyro_offsets(&mpu, gx_off + 20.0f, gy_off, gz_off);
    vTaskDelay(pdMS_TO_TICKS(1000));

    mpu6050_get_accel_offsets(&mpu, &ax_off, &ay_off, &az_off);
    mpu6050_get_gyro_offsets(&mpu, &gx_off, &gy_off, &gz_off);
    ESP_LOGI(TAG, "Nowe Offsety ACC: X=%.4f g, Y=%.4f g, Z=%.4f g", ax_off, ay_off, az_off);
    ESP_LOGI(TAG, "Nowe Offsety GYRO: X=%.4f dps, Y=%.4f dps, Z=%.4f dps", gx_off, gy_off, gz_off);

    ESP_LOGI(TAG, "Odczyt danych:");

    for(int i=0; i<5; i++) {
        mpu6050_read_data(&mpu, &data);
        ESP_LOGI(TAG, "ACC[g]: X:%.2f Y:%.2f Z:%.2f | GYRO[dps]: X:%.2f Y:%.2f Z:%.2f", 
                 data.accel_x, data.accel_y, data.accel_z, data.gyro_x, data.gyro_y, data.gyro_z);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    ESP_LOGI(TAG, "Czekam 10 sekund...");
    vTaskDelay(pdMS_TO_TICKS(10000));


    ESP_LOGI(TAG, "Zmiana offsetów do poziomu fabrycznego:");
    mpu6050_set_gyro_offsets(&mpu, gx_off - 20.0f, gy_off, gz_off);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // ========================================================================
    // KROK 7: Obsługa Przerwań - DEMO "BICIE SERCA" (Cycle Mode)
    // ========================================================================
    wait_and_explain("TEST PRZERWAN: Tryb Cycle Mode (Low Power 1.25Hz)", 3);

    

    // 4. URUCHAMIAMY CYCLE MODE (To jest klucz do "wolnego" przerwania)
    // Wymaga to: Włączenia Cycle bitu w PWR_MGMT_1 i wyłączenia Temp Sensora (wymóg Cycle Mode)
    // Domyślnie częstotliwość to 1.25 Hz (raz na 0.8s)
    mpu6050_set_temp_sensor(&mpu, false); // Wyłączamy temp (wymagane w Cycle Mode akcelerometru)
    mpu6050_set_cycle_mode(&mpu, true);   // Start cyklicznego wybudzania
    
    ESP_LOGI(TAG, "Czujnik w trybie Cycle Mode. Czekam na 'bicie serca' (INT)...");

    for(int i=0; i<10; i++) {
        // Czekamy aktywnie aż pin INT zmieni stan na WYSOKI (1)
        int timeout = 1000; // timeout zabezpieczający
        while(gpio_get_level(MPU_INT_PIN) == 0 && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Czekamy 10ms
            timeout--;
        }

        if(timeout > 0) {
            // WYKRYTO SYGNAŁ!
            ESP_LOGW(TAG, "[%d/10] PING! MPU obudzil sie i zglosil dane (INT=1)", i+1);
            
            // Szybki odczyt danych - to automatycznie SKASUJE pin INT do zera
            mpu6050_read_data(&mpu, &data);
            
            ESP_LOGI(TAG, "       Dane odczytane: AccZ=%.2f g. Pin INT powinien zgasnac.", data.accel_z);
        } else {
            ESP_LOGE(TAG, "Timeout! Nie wykryto przerwania.");
        }
        
        // Mała pauza, żeby nie zalać logów, chociaż w Cycle Mode i tak czekamy na czujnik
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Wyłączamy Cycle Mode na koniec, żeby wrócić do normy
    mpu6050_set_cycle_mode(&mpu, false); // Wyłącz tryb Cycle
    mpu6050_set_temp_sensor(&mpu, true); // Przywracamy temperaturę
    mpu6050_set_sleep_mode(&mpu, false); // Upewnij się, że Sleep jest wyłączony (Wake up)
    
    // Czekamy aż temperatura się ustabilizuje (czujnik temperatury potrzebuje czasu)
    ESP_LOGI(TAG, "Czekam 100ms na ustabilizowanie temperatury...");
    vTaskDelay(pdMS_TO_TICKS(100));

    // ========================================================================
    // KROK 8: FIFO (Burst Read)
    // ========================================================================
    wait_and_explain("Test FIFO (Buforowanie danych)", 2);

    // 1. Najpierw wyłączamy FIFO całkowicie
    mpu6050_set_fifo_enable(&mpu, false);
        
    // 2. Resetujemy zawartość FIFO (czyszczenie śmieci)
    mpu6050_reset_fifo(&mpu);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Ustawiamy konfigurację (Temp)
    mpu6050_fifo_enable_t fifo_cfg = { 
        .temp_fifo_en = true, 
        .accel_fifo_en = false, 
        .zg_fifo_en = false,    
        .xg_fifo_en = false, 
        .yg_fifo_en = false 
    };
    mpu6050_set_fifo_enable_config(&mpu, &fifo_cfg);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 4. Ponowny Reset FIFO (KLUCZOWE: aby nowa konfiguracja 'siadła' od zera)
    mpu6050_reset_fifo(&mpu);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 5. Dopiero teraz włączamy zbieranie
    ESP_LOGI(TAG, "Włączam FIFO na 200ms...");
    mpu6050_set_fifo_enable(&mpu, true);
    vTaskDelay(pdMS_TO_TICKS(200));

    mpu6050_set_fifo_enable(&mpu, false); // Stop zbierania
    vTaskDelay(pdMS_TO_TICKS(10)); // Krótka pauza przed odczytem

    uint16_t count;
    mpu6050_get_fifo_count(&mpu, &count);
    ESP_LOGI(TAG, "Zebrano %d bajtów w FIFO (Max 1024).", count);

    if(count > 0 && count < 1024) { // Dodatkowe zabezpieczenie
        // Sprawdzamy czy liczba bajtów jest podzielna przez 2 (każda próbka to 2 bajty: Temp_H, Temp_L)
        if (count % 2 != 0) {
            ESP_LOGW(TAG, "Uwaga: Liczba bajtów (%d) nie jest podzielna przez 2! Możliwe uszkodzenie danych.", count);
        }
        
        uint8_t buffer[32] = {0}; // Zerujemy bufor na starcie
        
        // Czytamy pierwsze 16 bajtów (czyli 8 próbek po 2 bajty) lub mniej jeśli count < 16
        size_t bytes_to_read = (count < 16) ? count : 16;
        esp_err_t ret = mpu6050_read_fifo(&mpu, buffer, bytes_to_read);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "--- ODCZYT DANYCH (HEX) ---");
            ESP_LOGI(TAG, "Odczytano %zu bajtów z FIFO", bytes_to_read);
            
            // Każda próbka to 2 bajty: [Temp_H, Temp_L]
            int num_samples = bytes_to_read / 2;
            for(int i=0; i<num_samples; i++) {
                int base = i*2;
                int16_t raw_temp = (int16_t)((buffer[base] << 8) | buffer[base+1]);
                
                // Konwersja przy użyciu funkcji biblioteki
                float temp_c = mpu6050_temp_to_celsius(raw_temp);
                
                ESP_LOGI(TAG, "Próbka %d: %02X %02X => Temp: %.1f C (raw %d)", 
                         i+1, 
                         buffer[base], buffer[base+1],
                         temp_c, raw_temp);
            }
            
        } else {
            ESP_LOGE(TAG, "Błąd odczytu I2C z FIFO!");
        }
    } else {
        ESP_LOGW(TAG, "Puste FIFO lub błąd licznika (Count: %d)", count);
    }

    // ========================================================================
    // KROK 9: Power Management (Sleep)
    // ========================================================================
    wait_and_explain("Koniec. Usypianie (Sleep Mode)", 2);
    mpu6050_set_sleep_mode(&mpu, true);
    ESP_LOGW(TAG, "Czujnik uśpiony. Pobór prądu minimalny.");

    mpu6050_int_enable_t dummy_status;
    mpu6050_get_int_status(&mpu, &dummy_status);
    ESP_LOGI(TAG, "Czekam 5s w uśpieniu przed krótkim wybudzeniem...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Krótkie wybudzenie, pojedynczy odczyt i ponowne uśpienie
    ESP_LOGI(TAG, "Wybudzam czujnik na 5s i wykonuję odczyt...");
    mpu6050_set_sleep_mode(&mpu, false);
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (mpu6050_read_data(&mpu, &data) == ESP_OK) {
        ESP_LOGI(TAG, "Odczyt po wybudzeniu: ACC[g] X=%.3f Y=%.3f Z=%.3f | TEMP=%.2f C",
                 data.accel_x, data.accel_y, data.accel_z, data.temp_c);
    }
    ESP_LOGI(TAG, "Kończę 5s wybudzenia, ponownie usypiam...");

    mpu6050_set_sleep_mode(&mpu, true);
    ESP_LOGI(TAG, "Ponownie uśpiono czujnik po odczycie.");

    mpu6050_int_enable_t dummy_status2;
    mpu6050_get_int_status(&mpu, &dummy_status2);



    // Deinit
    mpu6050_deinit(&mpu);
    vTaskDelete(NULL);
}

void mpu6050_test_start(void)
{
    xTaskCreate(mpu6050_test_task, "mpu_full_test", 4096, NULL, 5, NULL);
}