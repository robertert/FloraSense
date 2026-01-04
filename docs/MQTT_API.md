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

#### 2. Wilgotność

**Topic:** `florasense/{user_id}/{device_id}/sensor/humidity`

**Format JSON:**

```json
{
  "value": 65.32,
  "unit": "%",
  "sensor": 1
}
```

**Pola:**

- `value` (float) - Wilgotność względna w procentach
- `unit` (string) - Jednostka: "%"
- `sensor` (int) - Numer czujnika (1)

**Przykład:**

```json
{ "value": 65.32, "unit": "%", "sensor": 1 }
```

---

#### 3. Czujnik światła (Light Sensor 1)

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

#### 4. Czujnik gleby (Soil Moisture)

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

#### 5. Czujnik IR Obstacle (1)

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

#### 6. Czujnik dock (Dock Sensor)

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

#### 7. Czujnik Hall (Hall Sensor)

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

#### 8. Bateria (Mock)

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

Dane z czujników są publikowane co **10 sekund** (PUB_INTERVAL_MS).

**Zmiana interwału:** Edytuj `PUB_INTERVAL_MS` w pliku `mqtt_client.c`:

```c
#define PUB_INTERVAL_MS 10000  // 10 sekund
```

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

Wszystkie operacje są logowane z tagiem `flora-mqtt`:

```
I (xxx) flora-mqtt: MQTT connected to broker mqtt://172.20.10.2:1883!
I (xxx) flora-mqtt: Subscribed to: florasense/default_user/8813BF6983D0/cmd/water, ...
I (xxx) flora-mqtt: Published to florasense/default_user/8813BF6983D0/sensor/temp | msg_id=123 | {"value": 23.45, "unit": "C", "sensor": 1}
I (xxx) flora-mqtt: MOVE_CMD received: {"direction":"forward","distance":20}
I (xxx) MOTOR_CTRL: Przejeżdżanie 20.00 cm w kierunku 'forward' (prędkość=128, czas=819 ms, kalibracja=24.40 cm/s)
```

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

- **v1.0** - Podstawowa obsługa MQTT
  - Publikacja danych z czujników
  - Komendy sterujące silnikami
  - Konfiguracja USER_ID
  - Obsługa przejeżdżania określonej odległości
