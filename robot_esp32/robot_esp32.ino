#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>      
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "mbedtls/base64.h"   



const char* ssid = "bmassistant-win";
const char* password = "danczak1";
const char* server_ip = "192.168.0.174"; 
const uint16_t server_port = 5000;

unsigned long lastPollTime = 0;
const unsigned long pollInterval = 500;


#define ENA 13
#define IN1 4   
#define IN2 23

#define ENB 15
#define IN3 33
#define IN4 32

#define I2C_SDA 27
#define I2C_SCL 14
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define AUDIO_PIN 25


const int freq = 5000;
const int resolution = 8;
const int speedPWM = 150; // Prędkość jazdy


const int kickSpeed = 255;  
const int holdSpeed = 100;  
const int kickTime = 150;

// TRYMER!!!! 
const int trimSpeed = 120; 



#define AUDIO_BUFFER_SIZE 32000  
volatile uint8_t audioBuffer[AUDIO_BUFFER_SIZE];
volatile size_t audioLength = 0;
volatile size_t audioIndex = 0;
volatile bool audioPlaying = false;
hw_timer_t *audioTimer = NULL;

unsigned long actionEndTime = 0;
bool isMoving = false;
bool isTurning = false;


void updateDisplay(String text) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("--- ROBOT ---");
    display.println(text);
    display.display();
    Serial.println(text);
}

void playBeep() {
    #if defined(ESP32) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
        tone(AUDIO_PIN, 1000, 200); 
    #else
        for(int i = 0; i < 200; i++) {
            digitalWrite(AUDIO_PIN, HIGH);
            delayMicroseconds(500);
            digitalWrite(AUDIO_PIN, LOW);
            delayMicroseconds(500);
        }
    #endif
}

void IRAM_ATTR onAudioTimer() {
    if (audioPlaying && audioIndex < audioLength) {
        dacWrite(AUDIO_PIN, audioBuffer[audioIndex]);
        audioIndex++;
    } else if (audioPlaying && audioIndex >= audioLength) {
        audioPlaying = false;
        dacWrite(AUDIO_PIN, 128);
    }
}


void playAudioFromBase64(const char* b64Data, size_t b64Len) {
    audioPlaying = false;
    audioIndex = 0;
    audioLength = 0;
    
    size_t decodedLen = 0;
    mbedtls_base64_decode(NULL, 0, &decodedLen, (const unsigned char*)b64Data, b64Len);
    
    if (decodedLen > AUDIO_BUFFER_SIZE) {
        Serial.printf("Audio za duże! %d > %d bajtów. Przycinam.\n", decodedLen, AUDIO_BUFFER_SIZE);
        decodedLen = AUDIO_BUFFER_SIZE;
    }
    
    size_t actualLen = 0;
    int ret = mbedtls_base64_decode(
        (unsigned char*)audioBuffer, AUDIO_BUFFER_SIZE, &actualLen,
        (const unsigned char*)b64Data, b64Len
    );
    
    if (ret != 0) {
        Serial.printf("Błąd dekodowania base64: %d\n", ret);
        return;
    }
    
    Serial.printf("Audio zdekodowane: %d bajtów (%.1f sek)\n", actualLen, actualLen / 8000.0);
    audioLength = actualLen;
    audioIndex = 0;
    audioPlaying = true;
}



void wyprostujKola() {
    if (trimSpeed > 0) {
        digitalWrite(IN3, HIGH);
        digitalWrite(IN4, LOW);
        ledcWrite(ENB, trimSpeed);
    } else if (trimSpeed < 0) {
        digitalWrite(IN3, LOW);
        digitalWrite(IN4, HIGH);
        ledcWrite(ENB, -trimSpeed);
    } else {
        digitalWrite(IN3, LOW);
        digitalWrite(IN4, LOW);
        ledcWrite(ENB, 0);
    }
}

void stopMotors() {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    ledcWrite(ENA, 0);
    ledcWrite(ENB, 0);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
    
    isMoving = false;
    isTurning = false;
}

void moveForward(int durationMs) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    ledcWrite(ENA, speedPWM);
    
    wyprostujKola();

    actionEndTime = millis() + durationMs;
    isMoving = true;
    isTurning = false;
}

void moveBackward(int durationMs) {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    ledcWrite(ENA, speedPWM);

    wyprostujKola();

    actionEndTime = millis() + durationMs;
    isMoving = true;
    isTurning = false;
}

void turnLeft(int durationMs) {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
    ledcWrite(ENB, kickSpeed);
    delay(kickTime);
    ledcWrite(ENB, holdSpeed);
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    ledcWrite(ENA, speedPWM);

    actionEndTime = millis() + (durationMs > kickTime ? durationMs - kickTime : 0);
    isMoving = true;
    isTurning = true;
}

void turnRight(int durationMs) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    ledcWrite(ENB, kickSpeed);
    delay(kickTime);
    ledcWrite(ENB, holdSpeed);
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    ledcWrite(ENA, speedPWM);

    actionEndTime = millis() + (durationMs > kickTime ? durationMs - kickTime : 0);
    isMoving = true;
    isTurning = true;
}


void setup() {
    Serial.begin(115200);


    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);

    ledcAttach(ENA, freq, resolution);
    ledcAttach(ENB, freq, resolution);
    stopMotors();

    pinMode(AUDIO_PIN, OUTPUT);
    dacWrite(AUDIO_PIN, 128); 

    audioTimer = timerBegin(1000000); 
    timerAttachInterrupt(audioTimer, &onAudioTimer);
    timerAlarm(audioTimer, 125, true, 0); 

    Wire.begin(I2C_SDA, I2C_SCL);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("Nie znaleziono ekranu OLED!"));
    } else {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.cp437(true);
        updateDisplay("URUCHAMIANIE...");
    }


    WiFi.begin(ssid, password);
    String dots = "";
    while(WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        dots += ".";
        if(dots.length() > 5) dots = ".";
        updateDisplay("Wi-Fi...\n" + dots);
    }

    String connectedInfo = "WiFi Polaczone!\nIP: " + WiFi.localIP().toString();
    updateDisplay(connectedInfo);
    delay(2000);

    // Test połączenia TCP do serwera
    WiFiClient testClient;
    if (testClient.connect(server_ip, server_port)) {
        Serial.println("TCP polaczenie z serwerem OK");
        updateDisplay("TCP OK\nPolaczono!");
        testClient.stop();
    } else {
        Serial.println("TCP polaczenie z serwerem FAILED - sprawdz IP");
        updateDisplay("Blad TCP\nSprawdz IP");

    }
}

void pollServer() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = String("http://") + server_ip + ":" + String(server_port) + "/api/poll";
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        if (payload.length() > 0) {
            size_t docSize = payload.length() + 1024;
            // Zabezpieczenie przed próbą alokacji zbyt dużej ilości pamięci
            if (docSize > 90000) docSize = 90000; 
            DynamicJsonDocument doc(docSize);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                JsonArray arr = doc.as<JsonArray>();
                for (JsonObject item : arr) {
                    String eventName = item["event"];
                    JsonObject data = item["data"];

                    if (eventName == "robot_command") {
                        String cmd = data["command"];
                        int val = data["value"];
                        if (val == 0) val = 1;
                        
                        String msg = "Komenda: " + cmd + "\nWartosc: " + String(val);
                        updateDisplay(msg);
                        // playBeep(); // Usunięte, żeby nie pipczał jak szalony
                        
                        int duration = val * 1000;
                        if (cmd == "przód") moveForward(duration);
                        else if (cmd == "tył") moveBackward(duration);
                        else if (cmd == "lewo") turnLeft(duration);
                        else if (cmd == "prawo") turnRight(duration);
                        else if (cmd == "wyprostuj") {
                            wyprostujKola();
                            actionEndTime = millis() + duration;
                            isMoving = true;
                        }
                        else stopMotors();
                    }
                    else if (eventName == "chat_response") {
                        String text = data["response"];
                        updateDisplay("AI:\n" + text);
                    }
                    else if (eventName == "audio_response") {
                        const char* audioB64 = data["audio"];
                        if (audioB64) {
                            size_t b64Len = strlen(audioB64);
                            Serial.printf("Odebrano audio: %d znakow base64\n", b64Len);
                            updateDisplay("Mowie...");
                            playAudioFromBase64(audioB64, b64Len);
                        }
                    }
                }
            } else {
                Serial.print(F("Błąd parsowania poll JSON: "));
                Serial.println(error.f_str());
            }
        }
    }
    http.end();
}

void loop() {

    if (millis() - lastPollTime > pollInterval) {
        lastPollTime = millis();
        pollServer();
    }


    if (isMoving && millis() >= actionEndTime) {
        stopMotors();
    }
}