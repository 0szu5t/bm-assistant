#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "mbedtls/base64.h"

// ============================================================
// ZMIEŃ NA SWÓJ URL SERWERA (Vercel / homelab)
// ============================================================
const char* SERVER_URL = "https://bm-assistant.vercel.app"; // BEZ slash na końcu!

// Interwał pollowania (ms)
const unsigned long POLL_INTERVAL = 500;

// ============================================================
// Silniki L298N
// ============================================================
#define ENA 13
#define IN1 4
#define IN2 23
#define ENB 15
#define IN3 33
#define IN4 32

// ============================================================
// OLED SSD1306
// ============================================================
#define I2C_SDA 27
#define I2C_SCL 14
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================
// Audio (DAC + timer)
// ============================================================
#define AUDIO_PIN 25

const int freq       = 5000;
const int resolution = 8;
const int speedPWM   = 150;
const int kickSpeed  = 255;
const int holdSpeed  = 100;
const int kickTime   = 150;


const int WHEEL_CORRECTION = 0;  // <-- !!!!!

#define AUDIO_BUFFER_SIZE 32000
volatile uint8_t audioBuffer[AUDIO_BUFFER_SIZE];
volatile size_t  audioLength  = 0;
volatile size_t  audioIndex   = 0;
volatile bool    audioPlaying = false;
hw_timer_t      *audioTimer   = NULL;

// ============================================================
// Stan ruchu
// ============================================================
unsigned long actionEndTime = 0;
bool isMoving   = false;
bool isTurning  = false;
bool continuous = false;  // true = jedź bez limitu czasu (do STOP)

// ============================================================
// Kod parowania i timing
// ============================================================
String pairingCode = "";
unsigned long lastPollTime = 0;

// ============================================================
// Wewnętrzna kolejka komend (potrzebna dla sekwencji AI)
// ============================================================
struct RobotCommand { String cmd; int durationSec; };

const int CMD_QUEUE_MAX = 20;
RobotCommand cmdQueue[CMD_QUEUE_MAX];
int cmdQueueHead  = 0;
int cmdQueueTail  = 0;
int cmdQueueCount = 0;

void enqueueCommand(const String& cmd, int durationSec) {
    if (cmdQueueCount >= CMD_QUEUE_MAX) { Serial.println("[QUEUE] Pełna!"); return; }
    cmdQueue[cmdQueueTail] = {cmd, durationSec};
    cmdQueueTail = (cmdQueueTail + 1) % CMD_QUEUE_MAX;
    cmdQueueCount++;
}

bool dequeueCommand(RobotCommand& out) {
    if (cmdQueueCount == 0) return false;
    out = cmdQueue[cmdQueueHead];
    cmdQueueHead = (cmdQueueHead + 1) % CMD_QUEUE_MAX;
    cmdQueueCount--;
    return true;
}

void clearQueue() {
    cmdQueueHead = cmdQueueTail = cmdQueueCount = 0;
}

// ============================================================
// OLED
// ============================================================
void updateDisplay(const String& text) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("--- ROBOT ---");
    display.println(text);
    display.display();
    Serial.println(text);
}

void showPairingCode(const String& code) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("  KOD PAROWANIA:");
    display.setTextSize(2);
    display.setCursor(8, 22);
    display.println(code);
    display.setTextSize(1);
    display.setCursor(0, 52);
    display.println("wpisz na stronie!");
    display.display();
    Serial.println(">>> KOD: " + code + " <<<");
}

// ============================================================
// Audio
// ============================================================
void playBeep() {
    #if defined(ESP32) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
        tone(AUDIO_PIN, 1000, 200);
    #else
        for (int i = 0; i < 200; i++) {
            digitalWrite(AUDIO_PIN, HIGH); delayMicroseconds(500);
            digitalWrite(AUDIO_PIN, LOW);  delayMicroseconds(500);
        }
    #endif
}

void IRAM_ATTR onAudioTimer() {
    if (audioPlaying && audioIndex < audioLength) {
        dacWrite(AUDIO_PIN, audioBuffer[audioIndex++]);
    } else if (audioPlaying) {
        audioPlaying = false;
        dacWrite(AUDIO_PIN, 128);
    }
}

void playAudioFromBase64(const char* b64Data, size_t b64Len) {
    audioPlaying = false;
    audioIndex = audioLength = 0;
    size_t decodedLen = 0;
    mbedtls_base64_decode(NULL, 0, &decodedLen, (const unsigned char*)b64Data, b64Len);
    if (decodedLen > AUDIO_BUFFER_SIZE) decodedLen = AUDIO_BUFFER_SIZE;
    size_t actualLen = 0;
    int ret = mbedtls_base64_decode(
        (unsigned char*)audioBuffer, AUDIO_BUFFER_SIZE, &actualLen,
        (const unsigned char*)b64Data, b64Len
    );
    if (ret != 0) { Serial.printf("Błąd base64: %d\n", ret); return; }
    audioLength = actualLen;
    audioPlaying = true;
}

// ============================================================
// Silniki
// ============================================================
void wyprostujKola() {
    if      (WHEEL_CORRECTION > 0) { digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  ledcWrite(ENB, WHEEL_CORRECTION);  }
    else if (WHEEL_CORRECTION < 0) { digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); ledcWrite(ENB, -WHEEL_CORRECTION); }
    else                           { digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  ledcWrite(ENB, 0); }
}

void stopMotors() {
    digitalWrite(IN1, LOW); digitalWrite(IN2, LOW); ledcWrite(ENA, 0);
    digitalWrite(IN3, LOW); digitalWrite(IN4, LOW); ledcWrite(ENB, 0);
    isMoving = isTurning = false;
}

void moveForward(int ms) {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); ledcWrite(ENA, speedPWM);
    wyprostujKola();
    if (ms > 0) { actionEndTime = millis() + ms; continuous = false; }
    else        { actionEndTime = 0; continuous = true; }  // jedź bez limitu
    isMoving = true; isTurning = false;
}

void moveBackward(int ms) {
    digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); ledcWrite(ENA, speedPWM);
    wyprostujKola();
    if (ms > 0) { actionEndTime = millis() + ms; continuous = false; }
    else        { actionEndTime = 0; continuous = true; }
    isMoving = true; isTurning = false;
}

void turnLeft(int ms) {
    digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); ledcWrite(ENB, kickSpeed);
    delay(kickTime); ledcWrite(ENB, holdSpeed);
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  ledcWrite(ENA, speedPWM);
    if (ms > 0) { actionEndTime = millis() + (ms > kickTime ? ms - kickTime : 0); continuous = false; }
    else        { actionEndTime = 0; continuous = true; }
    isMoving = true; isTurning = true;
}

void turnRight(int ms) {
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); ledcWrite(ENB, kickSpeed);
    delay(kickTime); ledcWrite(ENB, holdSpeed);
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); ledcWrite(ENA, speedPWM);
    if (ms > 0) { actionEndTime = millis() + (ms > kickTime ? ms - kickTime : 0); continuous = false; }
    else        { actionEndTime = 0; continuous = true; }
    isMoving = true; isTurning = true;
}

void executeCommand(const String& cmd, int durationSec) {
    // durationSec == 0  → tryb ciągły (jedź do STOP)
    // durationSec  > 0  → jedź przez durationSec sekund
    int ms = durationSec * 1000;  // ms == 0 gdy ciągły

    if (durationSec > 0)
        updateDisplay("Cmd: " + cmd + "\n" + String(durationSec) + "s");
    else
        updateDisplay("Cmd: " + cmd + "\nciagle");

    if      (cmd == "przod" || cmd == "przód") moveForward(ms);
    else if (cmd == "tyl"   || cmd == "tył")   moveBackward(ms);
    else if (cmd == "lewo")                     turnLeft(ms);
    else if (cmd == "prawo")                    turnRight(ms);
    else if (cmd == "wyprostuj") { wyprostujKola(); if (ms > 0) { actionEndTime = millis() + ms; continuous = false; } else continuous = true; isMoving = true; }
    else stopMotors();
}

// ============================================================
// Rejestracja na serwerze cloud
// ============================================================
bool registerDevice() {
    WiFiClientSecure client;
    client.setInsecure(); // Akceptuj każdy cert SSL (dla hobbystycznego projektu wystarczy)

    HTTPClient http;
    String url = String(SERVER_URL) + "/api/register";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    String mac  = WiFi.macAddress();
    String body = "{\"device_id\":\"" + mac + "\"}";

    Serial.println("[REGISTER] POST -> " + url);
    int code = http.POST(body);

    if (code == 200) {
        String resp = http.getString();
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, resp)) {
            pairingCode = doc["code"].as<String>();
            int ttl = doc["ttl"] | 300;
            Serial.println("[REGISTER] OK  code=" + pairingCode + "  ttl=" + String(ttl) + "s");
            http.end();
            return true;
        }
    }
    Serial.printf("[REGISTER] Błąd HTTP: %d\n", code);
    http.end();
    return false;
}

// ============================================================
// Polling serwera
// ============================================================
void pollServer() {
    if (WiFi.status() != WL_CONNECTED || pairingCode.isEmpty()) return;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = String(SERVER_URL) + "/api/poll?code=" + pairingCode;
    http.begin(client, url);
    http.setTimeout(8000);

    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        if (payload.length() <= 2) { http.end(); return; } // pusta tablica []

        size_t docSize = min((size_t)(payload.length() + 1024), (size_t)90000);
        DynamicJsonDocument doc(docSize);
        DeserializationError err = deserializeJson(doc, payload);

        if (!err) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject item : arr) {
                String eventName = item["event"].as<String>();
                JsonObject data  = item["data"];

                if (eventName == "robot_command") {
                    String cmd = data["command"].as<String>();
                    int    val = data["value"]   | 1;

                    if (cmd == "stop") {
                        // STOP: natychmiast, wyczyść kolejkę
                        clearQueue();
                        stopMotors();
                        showPairingCode(pairingCode);
                    } else {
                        enqueueCommand(cmd, val);
                    }
                }
                else if (eventName == "chat_response") {
                    updateDisplay("AI:\n" + item["data"]["response"].as<String>());
                }
                else if (eventName == "audio_response") {
                    const char* audioB64 = data["audio"];
                    if (audioB64) { updateDisplay("Mowie..."); playAudioFromBase64(audioB64, strlen(audioB64)); }
                }
            }
        } else {
            Serial.print(F("[POLL] Błąd JSON: ")); Serial.println(err.f_str());
        }

    } else if (httpCode == 404) {
        // Sesja wygasła — re-rejestracja
        Serial.println("[POLL] Sesja wygasla, ponawiam rejestracje...");
        updateDisplay("Sesja wygasla\nRejestruje...");
        pairingCode = "";
        if (registerDevice()) showPairingCode(pairingCode);

    } else {
        Serial.printf("[POLL] HTTP %d\n", httpCode);
    }

    http.end();
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);

    // Silniki
    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
    ledcAttach(ENA, freq, resolution);
    ledcAttach(ENB, freq, resolution);
    stopMotors();

    // Audio
    pinMode(AUDIO_PIN, OUTPUT);
    dacWrite(AUDIO_PIN, 128);
    audioTimer = timerBegin(1000000);
    timerAttachInterrupt(audioTimer, &onAudioTimer);
    timerAlarm(audioTimer, 125, true, 0);

    // OLED
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("Nie znaleziono OLED!"));
    } else {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.cp437(true);
        updateDisplay("URUCHAMIANIE...");
    }

    // WiFiManager — bez pola IP (serwer cloud ma stały URL)
    updateDisplay("PORTAL WIFI\nPolacz: bm-assistant");
    WiFiManager wm;
    // wm.resetSettings(); // odkomentuj TYLKO gdy chcesz wymusić nowy portal WiFi

    if (!wm.autoConnect("bm-assistant")) {
        Serial.println("Nie udało się połączyć!");
        ESP.restart();
    }

    updateDisplay("WiFi OK!\nIP: " + WiFi.localIP().toString());
    delay(1500);

    // Rejestracja na serwerze cloud
    updateDisplay("Lacze z\nserwerem...");
    int attempts = 0;
    while (pairingCode.isEmpty() && attempts < 5) {
        if (registerDevice()) break;
        attempts++;
        updateDisplay("Ponawianie\n" + String(attempts) + "/5...");
        delay(2000);
    }

    if (pairingCode.isEmpty()) {
        updateDisplay("Blad serwera!\nSprawdz URL");
    } else {
        showPairingCode(pairingCode);
    }
}

// ============================================================
// Loop
// ============================================================
void loop() {
    // 1. Polluj serwer
    if (millis() - lastPollTime > POLL_INTERVAL) {
        lastPollTime = millis();
        pollServer();
    }

    // 2. Silniki skończyły — weź następną komendę z kolejki
    // (pomijamy jeśli tryb ciągły — czekamy wtedy na STOP)
    if (isMoving && !continuous && millis() >= actionEndTime) {
        stopMotors();
        RobotCommand next;
        if (dequeueCommand(next)) {
            executeCommand(next.cmd, next.durationSec);
        } else {
            showPairingCode(pairingCode); // wróć do wyświetlania kodu
        }
    }

    // 3. Nic nie gra a coś czeka w kolejce — zacznij
    if (!isMoving && cmdQueueCount > 0) {
        RobotCommand next;
        if (dequeueCommand(next)) {
            executeCommand(next.cmd, next.durationSec);
        }
    }
}