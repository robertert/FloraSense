# Dokumentacja API MQTT - FloraSense

## Spis treści

1. [Struktura topiców](#struktura-topiców)
2. [Publikacja danych z czujników](#publikacja-danych-z-czujników)
3. [Komendy sterujące](#komendy-sterujące)
4. [Konfiguracja](#konfiguracja)
5. [Przykłady użycia](#przykłady-użycia)

---

## Struktura topiców

### Format podstawowy

Wszystkie tematy używają następującej struktury:

```
florasense/{user_id}/{device_id}/{typ}/{nazwa}
```

Gdzie:

- `{user_id}` - ID użytkownika (domyślnie: `default_user`, można zmienić przez MQTT)
- `{device_id}` - ID urządzenia generowane z adresu MAC (np: `8813BF6983D0`)
- `{typ}` - Typ tematu: `sensor`, `state`, `cmd`, `config`
- `{nazwa}` - Konkretna nazwa czujnika/komendy

### Przykładowe tematy

```
florasense/default_user/8813BF6983D0/sensor/temp
florasense/default_user/8813BF6983D0/cmd/move
florasense/8813BF6983D0/config/user
```

---

## Publikacja danych z czujników

ESP32 publikuje dane z czujników co **10 sekund** (PUB_INTERVAL_MS).

### Tematy publikowane

#### 1. Temperatura

**Topic:** `florasense/{user_id}/{device_id}/sensor/temp`

**Format JSON:**

```json
{
  "value": 23.45,
  "unit": "C",
  "sensor": 1
}
```

**Pola:**

- `value` (float) - Temperatura w stopniach Celsjusza
- `unit` (string) - Jednostka: "C"
- `sensor` (int) - Numer czujnika (1)

**Przykład:**

```json
{ "value": 23.45, "unit": "C", "sensor": 1 }
```

---

#### 2. Czujnik światła (Light Sensor 1)

**Topic:** `florasense/{user_id}/{device_id}/sensor/light`

**Format JSON:**

```json
{
  "value": 1234.56,
  "unit": "lux",
  "sensor": 1
}
```

**Pola:**

- `value` (float) - Natężenie światła w luxach
- `unit` (string) - Jednostka: "lux"
- `sensor` (int) - Numer czujnika (1 lub 2)

**Przykład:**

```json
{ "value": 1234.56, "unit": "lux", "sensor": 1 }
```

**Uwaga:** Publikowane są dane z dwóch czujników światła (sensor: 1 i 2).

---

#### 3. Czujnik gleby (Soil Moisture)

**Topic:** `florasense/{user_id}/{device_id}/sensor/soil`

**Format JSON:**

```json
{
  "value": 45.67,
  "unit": "%",
  "mv": 1850,
  "sensor": 1
}
```

**Pola:**

- `value` (float) - Wilgotność gleby w procentach
- `unit` (string) - Jednostka: "%"
- `mv` (int) - Napięcie w miliwoltach (surowe dane)
- `sensor` (int) - Numer czujnika (1)

**Przykład:**

```json
{ "value": 45.67, "unit": "%", "mv": 1850, "sensor": 1 }
```

---

#### 4. Czujnik IR Obstacle (1)

**Topic:** `florasense/{user_id}/{device_id}/sensor/ir`

**Format JSON:**

```json
{
  "obstacle": true,
  "sensor": 1
}
```

**Pola:**

- `obstacle` (boolean) - `true` jeśli wykryto przeszkodę, `false` w przeciwnym razie
- `sensor` (int) - Numer czujnika (1 lub 2)

**Przykład:**

```json
{ "obstacle": true, "sensor": 1 }
```

**Uwaga:** Publikowane są dane z dwóch czujników IR (sensor: 1 i 2).

---

#### 5. Czujnik dock (Dock Sensor)

**Topic:** `florasense/{user_id}/{device_id}/state/dock`

**Format JSON:**

```json
{
  "state": "connected"
}
```

**Pola:**

- `state` (string) - Stan: `"connected"` lub `"disconnected"`

**Przykład:**

```json
{ "state": "connected" }
```

---

#### 6. Czujnik Hall (Hall Sensor)

**Topic:** `florasense/{user_id}/{device_id}/sensor/hall`

**Format JSON:**

```json
{
  "magnetic": true
}
```

**Pola:**

- `magnetic` (boolean) - `true` jeśli wykryto pole magnetyczne, `false` w przeciwnym razie

**Przykład:**

```json
{ "magnetic": true }
```

---

#### 7. Bateria (Mock)

**Topic:** `florasense/{user_id}/{device_id}/state/battery`

**Format JSON:**

```json
{
  "value": 75,
  "unit": "%"
}
```

**Pola:**

- `value` (int) - Poziom baterii w procentach (obecnie mock: 75%)
- `unit` (string) - Jednostka: "%"

**Przykład:**

```json
{ "value": 75, "unit": "%" }
```

---

#### 8. Alarmy (Alarms)

**Topic:** `florasense/{user_id}/{device_id}/alarm`

**Format JSON:**

```json
{
  "temp_low": {
    "value": 12.5,
    "threshold": 15.0,
    "unit": "C"
  },
  "soil_high": {
    "value": 85.0,
    "threshold": 80.0,
    "unit": "%"
  }
}
```

**Pola:**

Alarm jest publikowany tylko wtedy, gdy wartość przekroczy ustawiony próg. Może zawierać jeden lub więcej typów alarmów:

- `temp_low` (object, opcjonalne) - Alarm gdy temperatura jest poniżej `temp_min`
  - `value` (float) - Aktualna temperatura
  - `threshold` (float) - Ustawiony próg minimalny
  - `unit` (string) - Jednostka: "C"
- `temp_high` (object, opcjonalne) - Alarm gdy temperatura jest powyżej `temp_max`
  - `value` (float) - Aktualna temperatura
  - `threshold` (float) - Ustawiony próg maksymalny
  - `unit` (string) - Jednostka: "C"
- `soil_low` (object, opcjonalne) - Alarm gdy wilgotność gleby jest poniżej `soil_moisture_min`
  - `value` (float) - Aktualna wilgotność gleby
  - `threshold` (float) - Ustawiony próg minimalny
  - `unit` (string) - Jednostka: "%"
- `soil_high` (object, opcjonalne) - Alarm gdy wilgotność gleby jest powyżej `soil_moisture_max`
  - `value` (float) - Aktualna wilgotność gleby
  - `threshold` (float) - Ustawiony próg maksymalny
  - `unit` (string) - Jednostka: "%"
- `battery_low` (object, opcjonalne) - Alarm gdy poziom baterii jest poniżej `battery_min`
  - `value` (float) - Aktualny poziom baterii
  - `threshold` (float) - Ustawiony próg minimalny
  - `unit` (string) - Jednostka: "%"

**Opis:** Alarmy są publikowane automatycznie w tym samym interwale co dane z czujników, ale tylko gdy wartość przekroczy ustawiony próg. Jeśli kilka progów jest przekroczonych jednocześnie, wszystkie alarmy są publikowane w jednej wiadomości.

**Przykłady:**

Alarm niskiej temperatury:

```json
{
  "temp_low": {
    "value": 12.5,
    "threshold": 15.0,
    "unit": "C"
  }
}
```

Alarm wysokiej wilgotności gleby:

```json
{
  "soil_high": {
    "value": 85.0,
    "threshold": 80.0,
    "unit": "%"
  }
}
```

Wiele alarmów jednocześnie:

```json
{
  "temp_high": {
    "value": 32.5,
    "threshold": 30.0,
    "unit": "C"
  },
  "soil_low": {
    "value": 15.0,
    "threshold": 20.0,
    "unit": "%"
  }
}
```

**Uwaga:**

- Alarmy są publikowane tylko gdy konfiguracja alarmów jest ustawiona (przez topic `/config/alarm`)
- Jeśli wartość wraca do normalnego zakresu, alarm przestaje być publikowany
- Alarmy są sprawdzane w tym samym interwale co publikacja danych z czujników

---

## Komendy sterujące

ESP32 subskrybuje następujące tematy komend:

### 1. Komenda MOVE - Sterowanie silnikami

**Topic:** `florasense/{user_id}/{device_id}/cmd/move`

#### Format 1: Przejazd określonej odległości (JSON)

**Format JSON:**

```json
{
  "direction": "forward",
  "distance": 20.0,
  "speed": 128
}
```

**Pola:**

- `direction` (string, wymagane) - Kierunek: `"forward"`, `"backward"`, `"przód"`, `"wstecz"`, `"tył"`
- `distance` (float, wymagane) - Odległość w centymetrach (np. 20.0 dla 20 cm)
- `speed` (int, opcjonalne) - Prędkość silników (0-255), domyślnie 128

**Obsługiwane kierunki:**

- `"forward"` lub `"przód"` - Do przodu
- `"backward"`, `"wstecz"` lub `"tył"` - Do tyłu

**Przykłady:**

Przejazd 20 cm do przodu:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m '{"direction":"forward","distance":20}'
```

Przejazd 30 cm do tyłu z prędkością 150:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m '{"direction":"backward","distance":30,"speed":150}'
```

Przejazd 15 cm do przodu (domyślna prędkość):

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m '{"direction":"forward","distance":15}'
```

#### Format 2: Ciągłe działanie (prosty tekst)

**Format:** Prosty tekst bez JSON

**Obsługiwane komendy:**

- `"forward"` lub `"przód"` - Ciągła jazda do przodu
- `"backward"`, `"wstecz"` lub `"tył"` - Ciągła jazda do tyłu
- `"left"` lub `"lewo"` - Ciągły skręt w lewo
- `"right"` lub `"prawo"` - Ciągły skręt w prawo
- `"stop"` - Zatrzymanie silników

**Przykłady:**

Ciągła jazda do przodu:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m "forward"
```

Zatrzymanie:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m "stop"
```

**Uwaga:** W przypadku ciągłego działania, silniki będą działać do momentu wysłania komendy `"stop"`.

---

### 2. Komenda WATER - Podlewanie (przygotowana na przyszłość)

**Topic:** `florasense/{user_id}/{device_id}/cmd/water`

**Format JSON:**

```json
{
  "duration": 5000
}
```

**Pola:**

- `duration` (int) - Czas podlewania w milisekundach

**Przykład:**

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/water" \
  -m '{"duration":5000}'
```

**Uwaga:** Obecnie komenda jest tylko logowana, nie wykonuje żadnej akcji.

---

## Konfiguracja

### Zmiana USER_ID

**Topic:** `florasense/{device_id}/config/user`

**Format:** Prosty tekst (string)

**Opis:** Ustawia nowy USER_ID, który jest zapisywany w NVS i używany w tematach.

**Przykład:**

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/user" \
  -m "nowy_user"
```

Po ustawieniu, wszystkie tematy będą używać nowego `user_id`:

```
florasense/nowy_user/8813BF6983D0/sensor/temp
florasense/nowy_user/8813BF6983D0/cmd/move
```

**Uwaga:** Nowy USER_ID jest zapisywany w NVS i będzie używany po restarcie ESP32.

---

### Konfiguracja alarmów

**Topic:** `florasense/{device_id}/config/alarm`

**Format JSON:**

```json
{
  "temp_min": 15.0,
  "temp_max": 30.0,
  "soil_moisture_min": 20.0,
  "soil_moisture_max": 80.0,
  "battery_min": 20.0
}
```

**Pola:**

- `temp_min` (float, opcjonalne) - Minimalna temperatura w stopniach Celsjusza
- `temp_max` (float, opcjonalne) - Maksymalna temperatura w stopniach Celsjusza
- `soil_moisture_min` (float, opcjonalne) - Minimalna wilgotność gleby w procentach
- `soil_moisture_max` (float, opcjonalne) - Maksymalna wilgotność gleby w procentach
- `battery_min` (float, opcjonalne) - Minimalny poziom baterii w procentach (od tego progu miga LED i wysyłany jest alarm)

**Opis:** Ustawia progi alarmowe dla temperatury i wilgotności gleby. Wszystkie pola są opcjonalne - można wysłać tylko te, które chcesz zaktualizować.

**Przykład:**

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/alarm" \
  -m '{"temp_min":15.0,"temp_max":30.0,"soil_moisture_min":20.0,"soil_moisture_max":80.0}'
```

**Uwaga:**

- Konfiguracja alarmów jest zapisywana w NVS i będzie zachowana po restarcie ESP32.
- Po ustawieniu progów, alarmy są automatycznie publikowane na topic `florasense/{user_id}/{device_id}/alarm` gdy wartości przekroczą progi.
- Można wysłać tylko część pól - pozostałe wartości pozostaną bez zmian.

---

### Konfiguracja interwału pomiarów

**Topic:** `florasense/{device_id}/config/measurement`

**Format JSON:**

```json
{
  "measurement_interval_ms": 5000
}
```

**Pola:**

- `measurement_interval_ms` (int, wymagane) - Interwał publikacji danych z czujników w milisekundach (1000-600000, czyli 1 sekunda - 10 minut)

**Opis:** Ustawia interwał publikacji danych z czujników. Domyślna wartość to 10000 ms (10 sekund).

**Przykład:**

Ustawienie interwału na 5 sekund:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/measurement" \
  -m '{"measurement_interval_ms":5000}'
```

Ustawienie interwału na 30 sekund:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/measurement" \
  -m '{"measurement_interval_ms":30000}'
```

**Uwaga:**

- Konfiguracja interwału jest zapisywana w NVS i będzie zachowana po restarcie ESP32.
- Wartości poza zakresem 1000-600000 ms będą odrzucone z ostrzeżeniem w logach.

---

### Konfiguracja urządzenia

**Topic:** `florasense/{device_id}/config/device`

**Format JSON:**

```json
{
  "light_movement_enabled": true,
  "light_threshold": 500.0,
  "soil_humidity_threshold": 50.0,
  "water_enabled": true
}
```

**Pola:**

- `light_movement_enabled` (boolean, opcjonalne) - Włącza/wyłącza automatyczne poruszanie się w kierunku światła
- `light_threshold` (float, opcjonalne) - Próg różnicy światła w lux dla WSN controller (domyślnie 10.0)
- `soil_humidity_threshold` (float, opcjonalne) - Próg wilgotności gleby w procentach dla automatycznego podlewania (domyślnie 50.0)
- `water_enabled` (boolean, opcjonalne) - Włącza/wyłącza automatyczne podlewanie (gdy wilgotność < próg, jedzie do przodu do ściany)

**Opis:** Włącza lub wyłącza funkcję automatycznego poruszania się urządzenia w kierunku światła (light-seeking movement).

**Przykład:**

Włączenie automatycznego poruszania się:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/device" \
  -m '{"light_movement_enabled":true}'
```

Wyłączenie automatycznego poruszania się:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/device" \
  -m '{"light_movement_enabled":false}'
```

**Uwaga:**

- Konfiguracja urządzenia jest zapisywana w NVS i będzie zachowana po restarcie ESP32.
- Gdy `light_movement_enabled` jest ustawione na `false`, automatyczne poruszanie się w kierunku światła (WSN controller) jest wyłączone i silniki są zatrzymywane.
- Gdy `light_movement_enabled` jest ustawione na `true`, urządzenie automatycznie porusza się w kierunku lepszego światła zgodnie z konfiguracją WSN controller.

---

## Przykłady użycia

### 1. Monitorowanie wszystkich danych

Subskrypcja wszystkich tematów:

```bash
mosquitto_sub -h 172.20.10.2 -p 1883 -t "florasense/#" -v
```

Subskrypcja tylko danych z czujników:

```bash
mosquitto_sub -h 172.20.10.2 -p 1883 -t "florasense/+/+/sensor/+" -v
```

Subskrypcja tylko stanów:

```bash
mosquitto_sub -h 172.20.10.2 -p 1883 -t "florasense/+/+/state/+" -v
```

---

### 2. Sterowanie robotem

#### Przejazd 20 cm do przodu:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m '{"direction":"forward","distance":20}'
```

#### Przejazd 50 cm do tyłu:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m '{"direction":"backward","distance":50}'
```

#### Ciągła jazda do przodu:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m "forward"
```

#### Zatrzymanie:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/default_user/8813BF6983D0/cmd/move" \
  -m "stop"
```

---

### 3. Zmiana konfiguracji

#### Zmiana USER_ID:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/user" \
  -m "moj_user"
```

#### Konfiguracja alarmów:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/alarm" \
  -m '{"temp_min":15.0,"temp_max":30.0,"soil_moisture_min":20.0,"soil_moisture_max":80.0}'
```

#### Zmiana interwału pomiarów:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/measurement" \
  -m '{"measurement_interval_ms":5000}'
```

#### Konfiguracja urządzenia:

```bash
mosquitto_pub -h 172.20.10.2 -p 1883 \
  -t "florasense/8813BF6983D0/config/device" \
  -m '{"light_movement_enabled":true}'
```

---

## Kalibracja silników

W pliku `config.h` znajduje się parametr kalibracji:

```c
#define MOTOR_CM_PER_SECOND_128  24.4f
```

**Opis:** Określa ile centymetrów robot przejeżdża w ciągu 1 sekundy przy prędkości 128.

**Jak skalibrować:**

1. Zmierz rzeczywistą odległość, którą robot przejeżdża w 1 sekundę przy prędkości 128
2. Zaktualizuj wartość `MOTOR_CM_PER_SECOND_128` w pliku `config.h`
3. Przekompiluj i wgraj firmware

**Przykład:**
Jeśli robot przejeżdża 25 cm w 1 sekundę przy prędkości 128, ustaw:

```c
#define MOTOR_CM_PER_SECOND_128  25.0f
```

---

## Konfiguracja brokera MQTT

**Domyślny broker:** `mqtt://172.20.10.2:1883`

**Zmiana brokera:** Edytuj `MQTT_BROKER_URI` w pliku `mqtt_client.c`:

```c
#define MQTT_BROKER_URI "mqtt://172.20.10.2:1883"
```

---

## Interwał publikacji

Dane z czujników są publikowane co **10 sekund** domyślnie (można zmienić przez MQTT).

**Zmiana interwału:**

- Przez MQTT: Wyślij konfigurację do tematu `florasense/{device_id}/config/measurement` (patrz sekcja [Konfiguracja interwału pomiarów](#konfiguracja-interwału-pomiarów))
- W kodzie: Domyślna wartość jest zdefiniowana jako `DEFAULT_MEASUREMENT_INTERVAL_MS` w pliku `mqtt_client.c`

**Uwaga:** Interwał jest zapisywany w NVS i będzie zachowany po restarcie ESP32.

---

## Obsługa błędów

### Brak połączenia z brokerem

ESP32 automatycznie próbuje ponownie połączyć się z brokerem co 5 sekund.

### Brak zainicjalizowanego czujnika

Jeśli czujnik nie jest zainicjalizowany, dane z niego nie są publikowane (bez błędu).

### Nieprawidłowa komenda

Nieprawidłowe komendy są logowane jako ostrzeżenie, ale nie przerywają działania.

---

## Logi ESP32

Wszystkie operacje są logowane z tagiem `flora-mqtt` z **timestampami** w formacie `[YYYY-MM-DD HH:MM:SS.mmm]`:

```
I (xxx) flora-mqtt: [2024-01-15 14:23:45.123] MQTT connected to broker mqtt://172.20.10.2:1883!
I (xxx) flora-mqtt: [2024-01-15 14:23:45.456] Subscribed to: florasense/default_user/8813BF6983D0/cmd/water, ...
I (xxx) flora-mqtt: [2024-01-15 14:23:50.789] Published to florasense/default_user/8813BF6983D0/sensor/temp | msg_id=123 | {"value": 23.45, "unit": "C", "sensor": 1}
I (xxx) flora-mqtt: [2024-01-15 14:24:00.012] MOVE_CMD received: {"direction":"forward","distance":20}
I (xxx) MOTOR_CTRL: Przejeżdżanie 20.00 cm w kierunku 'forward' (prędkość=128, czas=819 ms, kalibracja=24.40 cm/s)
```

**Uwaga:** Timestampy są dodawane do wszystkich logów MQTT (połączenia, publikacje, komendy, błędy) dla łatwiejszego śledzenia i debugowania.

---

## Wymagania

- Broker MQTT (np. Mosquitto)
- Biblioteka `mosquitto-clients` do testowania (opcjonalnie):

  ```bash
  # macOS
  brew install mosquitto

  # Linux
  sudo apt-get install mosquitto-clients
  ```

---

## Uwagi

1. **DEVICE_ID** jest generowane z adresu MAC i jest unikalne dla każdego urządzenia
2. **USER_ID** można zmienić przez MQTT i jest zapisywany w NVS
3. Wszystkie dane są publikowane w formacie JSON
4. Komendy mogą być w formacie JSON lub prostym tekście
5. Kalibracja silników jest wymagana dla dokładnego przejeżdżania odległości

---

## Changelog

- **v1.4** - Rozszerzenie konfiguracji i automatyczne podlewanie

  - Dodano `battery_min` do konfiguracji alarmów - konfigurowalny próg niskiego poziomu baterii
  - LED ostrzegawczy baterii używa teraz dynamicznego progu z konfiguracji alarmów
  - Rozszerzono konfigurację urządzenia o `light_threshold`, `soil_humidity_threshold`, `water_enabled`
  - Implementacja automatycznego podlewania - jazda do przodu przy niskiej wilgotności gleby
  - WSN controller używa teraz `light_threshold` z konfiguracji MQTT

- **v1.3** - Dodano automatyczne publikowanie alarmów i integrację z WSN controller

  - Automatyczne publikowanie alarmów na topic `florasense/{user_id}/{device_id}/alarm` gdy wartości przekroczą progi
  - Integracja flagi `light_movement_enabled` z WSN controller - automatyczne poruszanie się w kierunku światła jest kontrolowane przez MQTT
  - Alarmy są publikowane w tym samym interwale co dane z czujników
  - WSN controller sprawdza flagę `light_movement_enabled` i zatrzymuje się gdy jest wyłączona

- **v1.2** - Dodano nowe tematy konfiguracji MQTT

  - Konfiguracja alarmów (`/config/alarm`) - ustawianie progów temperatury i wilgotności gleby
  - Konfiguracja pomiarów (`/config/measurement`) - dynamiczna zmiana interwału publikacji danych
  - Konfiguracja urządzenia (`/config/device`) - włączanie/wyłączanie automatycznego poruszania się w kierunku światła
  - Wszystkie konfiguracje są zapisywane w NVS i zachowane po restarcie
  - Interwał publikacji jest teraz dynamiczny i konfigurowalny przez MQTT

- **v1.1** - Dodano timestampy do logów MQTT

  - Wszystkie logi zawierają timestamp w formacie `[YYYY-MM-DD HH:MM:SS.mmm]`
  - Ułatwia debugowanie i śledzenie zdarzeń w czasie

- **v1.0** - Podstawowa obsługa MQTT
  - Publikacja danych z czujników
  - Komendy sterujące silnikami
  - Konfiguracja USER_ID
  - Obsługa przejeżdżania określonej odległości
