# BM Assistant — Dokumentacja Projektu

Robot sterowany przez internet, wyposażony w AI, oparty na mikrokontrolerze ESP32.

---

## Spis treści

1. [Opis projektu](#opis-projektu)
2. [Architektura systemu](#architektura-systemu)
3. [Struktura repozytorium](#struktura-repozytorium)
4. [Szybki start](#szybki-start)
5. [Dokumenty szczegółowe](#dokumenty-szczegółowe)

---

## Opis projektu

BM Assistant to robot mobilny z silnikami DC sterowany zdalnie przez przeglądarkę internetową. Użytkownik może:

- sterować robotem przyciskami (D-pad) lub joystickiem na telefonie/komputerze,
- wydawać polecenia w języku naturalnym (np. „jedź do przodu 2 sekundy, skręć w prawo") — model AI przetłumaczy je na sekwencję ruchów,
- monitorować stan robota na wbudowanym wyświetlaczu OLED.

Komunikacja odbywa się przez chmurowy serwer (Vercel + Redis) — robot i przeglądarka nie muszą być w tej samej sieci.

---

## Architektura systemu

```
┌─────────────────────────────────────────────────────────────────────┐
│                         PRZEGLĄDARKA                                │
│  (telefon / komputer)   panel sterowania    pole AI                 │
└──────────────┬──────────────────────────────────────┬──────────────┘
               │  HTTPS REST                          │  HTTPS REST
               ▼                                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     CLOUD SERVER (Vercel)                           │
│  Flask  ·  Redis (kolejka zdarzeń)  ·  NVIDIA API (LLM)            │
│  URL: https://bm-assistant.vercel.app                               │
└──────────────────────────────────┬──────────────────────────────────┘
                                   │  HTTPS polling (co 250 ms)
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                          ESP32 (robot)                              │
│  Silniki DC  ·  Mostek L298N  ·  OLED  ·  Audio PAM8403            │
└─────────────────────────────────────────────────────────────────────┘
```

### Przepływ sesji

1. Robot startuje → łączy się z WiFi przez portal konfiguracyjny `bm-assistant`.
2. Rejestruje się na serwerze cloud → otrzymuje unikalny kod parowania `XXX-XXX`.
3. Kod pojawia się na wyświetlaczu OLED.
4. Użytkownik wchodzi na stronę, wpisuje kod → trafia do panelu sterowania.
5. Polecenia z panelu trafiają do Redis; robot pobiera je co 250 ms (polling).
6. Sesja wygasa po 300 s braku aktywności robota; robot automatycznie re-rejestruje się.

### Przepływ AI

1. Użytkownik wpisuje polecenie tekstowe w panelu.
2. Cloud server przekazuje je do NVIDIA API (model `meta/llama-3.1-8b-instruct`).
3. LLM zwraca tablicę JSON z krokami sekwencji, np. `[{"cmd":"F","time":2},{"cmd":"R","time":1}]`.
4. Serwer umieszcza kroki w kolejce Redis.
5. ESP32 pobiera je po kolei i wykonuje każdy krok przez zadaną liczbę sekund.

---

## Struktura repozytorium

```
ff768292/
├── robot_esp32/
│   └── robot_esp32.ino       # Firmware ESP32 (Arduino)
│
├── cloud_server/             # Serwer chmurowy (Vercel)
│   ├── app.py
│   ├── requirements.txt
│   ├── vercel.json
│   ├── .env.example
│   └── templates/
│       ├── home.html         # Strona logowania (wpisywanie kodu)
│       └── control.html      # Panel sterowania
│
├── laptop_server/            # Serwer lokalny (sieć domowa)
│   ├── app.py
│   ├── requirements.txt
│   ├── .env.example
│   └── templates/
│       └── index.html        # Panel sterowania (wersja lokalna)
│
├── homelab_server/           # Serwer lokalny z SQLite
│   └── .env
│
├── test_robota/
│   └── README.txt            # Procedury bezpieczeństwa i awarii
│
├── dokumentacja/             # ← jesteś tutaj
│   ├── README.md             # Ten plik — przegląd projektu
│   ├── hardware.md           # Sprzęt, schematy, piny
│   ├── firmware.md           # Firmware ESP32 — funkcje i logika
│   ├── api.md                # Dokumentacja REST API
│   ├── konfiguracja.md       # Przewodnik konfiguracji i wdrożenia
│   └── bezpieczenstwo.md     # Zasady bezpiecznej pracy ze sprzętem
│
└── opis.txt                  # Ogólny opis projektu
```

---

## Szybki start

### Wymagania

- Konto NVIDIA Developer z kluczem API → [build.nvidia.com](https://build.nvidia.com/)
- Konto Vercel (darmowe) + Redis (np. Upstash)
- Arduino IDE z obsługą ESP32
- Python 3.8+

### Kroki

1. **Sklonuj repozytorium** i przejdź do folderu `cloud_server`.
2. Skopiuj `.env.example` → `.env`, uzupełnij `NVIDIA_API_KEY` i `REDIS_URL`.
3. Wdróż na Vercel: `vercel deploy` lub przez panel webowy.
4. W pliku `robot_esp32/robot_esp32.ino` ustaw `SERVER_URL` na adres swojego wdrożenia.
5. Wgraj firmware do ESP32 (szczegóły → [konfiguracja.md](konfiguracja.md)).
6. Włącz robota — na OLED pojawi się kod parowania.
7. Wejdź na stronę serwera, wpisz kod → gotowe.

Szczegółowy przewodnik krok po kroku: **[konfiguracja.md](konfiguracja.md)**

---

## Dokumenty szczegółowe

| Plik | Zawartość |
|------|-----------|
| [hardware.md](hardware.md) | Komponenty, schemat pinów ESP32, opis układów elektronicznych |
| [firmware.md](firmware.md) | Funkcje firmware, obsługa kolejki komend, protokół komunikacji |
| [api.md](api.md) | Endpointy REST API cloud servera i laptop servera |
| [konfiguracja.md](konfiguracja.md) | Instalacja, konfiguracja zmiennych środowiskowych, wdrożenie |
| [bezpieczenstwo.md](bezpieczenstwo.md) | Zasady pracy ze sprzętem, procedury wgrywania, księga awarii |
