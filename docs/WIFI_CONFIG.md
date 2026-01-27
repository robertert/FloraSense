# Konfiguracja WiFi i zmiana właściciela

## Tryby pracy WiFi

### 1. Tryb STA (Station) - normalna praca
- Urządzenie łączy się z zapisaną siecią WiFi
- Publikuje dane przez MQTT

### 2. Tryb AP (Access Point) - konfiguracja
- SSID: `ESP32_SETUP` (otwarta sieć)
- Serwer HTTP na porcie 80, endpoint `/save`
- Wymaga tokenu autoryzacji w nagłówku `X-Auth-Token` (MAC urządzenia bez dwukropków)

---

## Wejście w tryb AP

Urządzenie wchodzi w tryb AP gdy:
1. **Brak zapisanej konfiguracji** - pierwsze uruchomienie
2. **Przycisk BOOT przytrzymany 5 sekund** - czyści tylko konfigurację WiFi (zachowuje user_id, alarmy, itp.)
3. **Przycisk BOOT wciśnięty przy starcie** - przez pierwsze 2.5s po włączeniu

---

## Konfiguracja przez aplikację

**Request POST na `/save`:**
```
Headers: X-Auth-Token: <MAC_ADDRESS>
Body: ssid=...&pass=...&user=<user_id>
```

**Co się dzieje:**
1. Zapisuje `ssid`, `pass`, `configured` do NVS
2. Zapisuje `user_id` do NVS
3. Restartuje urządzenie

---

## Zmiana właściciela (user_id)

**Przy każdym starcie MQTT (`mqtt_client.c`):**
```
Publikuje na topic: florasense/{device_id}/config/user
Payload: {user_id}
```

Backend odbiera to i aktualizuje właściciela urządzenia w bazie danych.

---

## Przechowywanie w NVS (namespace: "storage")

| Klucz | Opis | Czyszczone przy reset? |
|-------|------|------------------------|
| `ssid` | Nazwa sieci WiFi | Tak |
| `pass` | Hasło WiFi | Tak |
| `configured` | Flaga konfiguracji | Tak |
| `user_id` | ID właściciela | Nie |
| `alarm_config` | Progi alarmów | Nie |
| `meas_int_ms` | Interwał pomiarów | Nie |
| `light_mov_en` | Włączone śledzenie światła | Nie |
| `water_enabled` | Automatyczne podlewanie | Nie |

---

## Flow zmiany właściciela

```
1. Użytkownik przytrzymuje BOOT 5s
2. → clear_nvs_config() czyści tylko WiFi
3. → esp_restart()
4. → Urządzenie startuje w trybie AP
5. Nowy użytkownik konfiguruje przez aplikację (ssid + user_id)
6. → Restart, połączenie z WiFi
7. → MQTT connect → publish_current_user()
8. → Backend aktualizuje właściciela
```
