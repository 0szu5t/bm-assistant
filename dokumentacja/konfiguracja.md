# Konfiguracja i wdrożenie — BM Assistant

Krok po kroku: jak skonfigurować i uruchomić cały system.

---

## Wymagania wstępne

### Software

- Python 3.8+ (z obsługą `venv`)
- Arduino IDE 2.x z obsługą ESP32 (esp32 by Espressif Systems ≥ 3.0)
- Git
- Konto Vercel (darmowe) — [vercel.com](https://vercel.com)
- Konto NVIDIA Developer — [build.nvidia.com](https://build.nvidia.com)
- Redis (np. Upstash dla Vercel, lub `redis://localhost:6379` dla homelab)

### Biblioteki Arduino

Zainstaluj przez **Arduino IDE → Narzędzia → Zarządzaj bibliotekami**:

| Biblioteka | Autor |
|------------|-------|
| `WiFiManager` | tzapu |
| `ArduinoJson` | Benoit Blanchon |
| `Adafruit SSD1306` | Adafruit |
| `Adafruit GFX Library` | Adafruit |

Biblioteki `WiFi`, `WiFiClientSecure`, `HTTPClient`, `mbedtls/base64` są wbudowane w pakiet esp32.

---

## 1. Konfiguracja Cloud Servera (Vercel)

### 1a. Klonowanie i konfiguracja lokalna

```bash
git clone <url-repozytorium>
cd ff768292/cloud_server

# Utwórz środowisko wirtualne
python -m venv .venv
source .venv/bin/activate        # Linux/Mac
# .venv\Scripts\activate         # Windows

pip install -r requirements.txt
```

Skopiuj plik środowiskowy:

```bash
cp .env.example .env
```

Edytuj `.env`:

```env
# Redis od Upstash (format: redis://default:<hasło>@<host>:<port>)
# lub lokalny: redis://localhost:6379
REDIS_URL=redis://localhost:6379

# Czas wygaśnięcia sesji w sekundach (domyślnie 5 minut)
SESSION_TTL_SECONDS=300

# Klucz z https://build.nvidia.com/
NVIDIA_API_KEY=nvapi-...
```

### 1b. Uruchomienie lokalne (testowanie)

```bash
python app.py
# Serwer dostępny pod http://localhost:5000
```

### 1c. Wdrożenie na Vercel

Zainstaluj Vercel CLI:

```bash
npm i -g vercel
```

Wdróż:

```bash
cd cloud_server
vercel deploy --prod
```

W panelu Vercel → Settings → Environment Variables dodaj:
- `NVIDIA_API_KEY` — klucz NVIDIA
- `REDIS_URL` — URL Redis (np. Upstash)
- `SESSION_TTL_SECONDS` — opcjonalnie (domyślnie 300)

> **Upstash Redis** — darmowy plan: [console.upstash.com](https://console.upstash.com).  
> Skopiuj „REST URL" jako `REDIS_URL`.

---

## 2. Konfiguracja Laptop Servera (sieć lokalna)

Używany zamiast cloud servera gdy robot i komputer są w tej samej sieci.

```bash
cd ff768292/laptop_server

python -m venv .venv
source .venv/bin/activate

pip install -r requirements.txt

cp .env.example .env
# Edytuj .env: NVIDIA_API_KEY=nvapi-...

python app.py
# Serwer dostępny pod http://<IP-laptopa>:5000
# Rozgłasza IP przez UDP broadcast na port 5555
```

Zmień `SERVER_URL` w firmware ESP32 na `http://<IP-laptopa>:5000` (bez HTTPS).

---

## 3. Konfiguracja Firmware ESP32

### 3a. Ustawienie adresu serwera

Otwórz `robot_esp32/robot_esp32.ino` w Arduino IDE.  
Znajdź i zmień linię:

```cpp
const char* SERVER_URL = "https://bm-assistant.vercel.app";
```

Wpisz URL swojego wdrożenia Vercel lub adres laptop servera.

### 3b. Wgrywanie firmware {#wgrywanie-firmware}

> **KRYTYCZNE: Zawsze przestrzegaj tej kolejności!**

1. **Odłącz piny ENA i ENB** z ESP32 (przewody oznaczone białą taśmą).
2. **Włącz zasilanie akumulatorów** — przełącznik na ON.
3. **Podłącz kabel USB** do ESP32.
4. W Arduino IDE: **Narzędzia → Płytka → ESP32 Dev Module** (lub odpowiedni model).
5. Kliknij **Wgraj (→)**.
6. Gdy zobaczysz `Connecting...` → wciśnij i **trzymaj przycisk BOOT** na ESP32 aż do końca wgrywania.
7. Po komunikacie sukcesu: **odłącz USB**, **wepnij ENA i ENB** z powrotem.

> Jeśli pomylisz kolejność ENA/ENB — patrz [bezpieczenstwo.md → Awaria 2](bezpieczenstwo.md#awaria-2).

### 3c. Konfiguracja WiFi (pierwsze uruchomienie)

1. Włącz robota (przełącznik ON).
2. Na OLED pojawi się `PORTAL WIFI / Polacz: bm-assistant`.
3. Na telefonie/komputerze połącz się z siecią WiFi o nazwie **`bm-assistant`**.
4. Automatycznie otworzy się portal konfiguracyjny (lub wejdź na `http://192.168.4.1`).
5. Wybierz swoją sieć domową i wprowadź hasło → Zapisz.
6. Robot restartuje się i łączy z wybraną siecią.
7. Na OLED pojawi się kod parowania `XXX-XXX`.

### 3d. Zmiana sieci WiFi

Jeśli chcesz zmienić sieć:
1. Odkomentuj `wm.resetSettings();` w `setup()` przed wywołaniem `wm.autoConnect(...)`.
2. Wgraj firmware.
3. Po uruchomieniu portalu → skonfiguruj nową sieć.
4. Zakomentuj `wm.resetSettings()` ponownie i wgraj firmware jeszcze raz.

---

## 4. Pierwsze uruchomienie całego systemu

1. ✅ Cloud server wdrożony na Vercel z uzupełnionymi zmiennymi środowiskowymi.
2. ✅ Firmware wgrany do ESP32 z poprawnym `SERVER_URL`.
3. ✅ WiFi skonfigurowane na robocie.
4. Włącz robota — na OLED pojawi się kod `XXX-XXX`.
5. Wejdź na `https://bm-assistant.vercel.app` (lub Twój URL).
6. Wpisz kod → kliknij **Połącz z Robotem**.
7. Użyj D-pada, joystick lub pola AI do sterowania.

---

## 5. Zmienne środowiskowe — podsumowanie

### `cloud_server/.env`

| Zmienna | Domyślna | Opis |
|---------|----------|------|
| `NVIDIA_API_KEY` | *(wymagana)* | Klucz API NVIDIA |
| `REDIS_URL` | `redis://localhost:6379` | Adres Redis |
| `SESSION_TTL_SECONDS` | `300` | Czas życia sesji (sekundy) |

### `laptop_server/.env`

| Zmienna | Domyślna | Opis |
|---------|----------|------|
| `NVIDIA_API_KEY` | *(wymagana)* | Klucz API NVIDIA |

---

## 6. Diagnostyka

### Sprawdzenie stanu cloud servera

```bash
curl https://bm-assistant.vercel.app/api/health
```

Oczekiwana odpowiedź:
```json
{ "status": "ok", "redis": true, "session_ttl": 300 }
```

### Serial Monitor ESP32

Ustaw baudrate na **115200**. Logi kluczowych zdarzeń:

```
[REGISTER] POST -> https://bm-assistant.vercel.app/api/register
[REGISTER] OK  code=ABC-123  ttl=300s
>>> KOD: ABC-123 <<<
[POLL] HTTP 200
[POLL] Sesja wygasla, ponawiam rejestracje...
```

### Typowe problemy

| Objaw | Przyczyna | Rozwiązanie |
|-------|-----------|-------------|
| OLED: `Blad serwera! Sprawdz URL` | Zły `SERVER_URL` lub serwer niedostępny | Sprawdź URL i deploy Vercel |
| `redis: false` w `/api/health` | Brak połączenia z Redis | Sprawdź `REDIS_URL` w zmiennych Vercel |
| Arduino IDE: `PermissionError` przy wgrywaniu | ENA/ENB wpięte, zasilanie OFF | Przestrzegaj procedury wgrywania |
| Robot nie reaguje po wpisaniu kodu | Sesja wygasła (>300 s) | Odczytaj nowy kod z OLED |
| Brak AI (`Brak klucza NVIDIA_API_KEY`) | Nie ustawiono klucza | Uzupełnij `NVIDIA_API_KEY` |
