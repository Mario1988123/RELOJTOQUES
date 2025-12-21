/*
 * RELOJ TOQUES - Con Pantalla + Toques + WiFi
 *
 * Hardware: ESP32-S3 + QMI8658 + Display
 *
 * Funcionalidad:
 * 1. Muestra reloj en pantalla
 * 2. Detecta toques para seleccionar carta (palo + número)
 * 3. Transmite carta por WiFi beacon
 */

#include <Wire.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <TFT_eSPI.h>

// Pines I2C
#define SDA_PIN 18
#define SCL_PIN 8

// QMI8658 Registers
#define QMI_ADDR 0x6B
#define QMI_WHOAMI 0x00
#define QMI_CTRL7 0x08
#define QMI_ACCEL_X 0x35

// Detección de toques
#define THRESHOLD 1500.0f
#define COOLDOWN 200
#define PAUSE 2500

// Caracteres invisibles para WiFi SSID
#define CH1 "\xE2\x80\x8B"  // U+200B
#define CH2 "\xE2\x80\x8C"  // U+200C
#define CH3 "\xE2\x80\x8D"  // U+200D
#define CH4 "\xE2\x80\x8E"  // U+200E

// Pantalla
TFT_eSPI tft = TFT_eSPI();

enum State { IDLE, SUIT, WAIT, NUMBER };
State state = IDLE;
uint8_t tapCount = 0;
unsigned long lastTap = 0;
uint8_t suit = 0, number = 0;

// Símbolos de palos
const char* suitSymbols[] = {"", "♥", "♠", "♣", "♦"};
const char* numberNames[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== RELOJ TOQUES ===");

  // Init Pantalla
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  // Mensaje de bienvenida
  tft.setCursor(20, 60);
  tft.println("RELOJ");
  tft.setCursor(15, 90);
  tft.println("TOQUES");

  // Init I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // Init QMI8658
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(QMI_CTRL7);
  Wire.write(0x03); // Enable accel
  Wire.endTransmission();

  // Init WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32", "", 1, 0, 0);

  Serial.println("Sistema listo. Esperando toques...");

  delay(2000);
  updateDisplay();
}

void loop() {
  // Leer acelerómetro
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(QMI_ACCEL_X);
  Wire.endTransmission(false);
  Wire.requestFrom(QMI_ADDR, 6);

  int16_t x = Wire.read() | (Wire.read() << 8);
  int16_t y = Wire.read() | (Wire.read() << 8);
  int16_t z = Wire.read() | (Wire.read() << 8);

  float ax = x * 4000.0 / 32768.0;
  float ay = y * 4000.0 / 32768.0;
  float az = z * 4000.0 / 32768.0;

  float mag = sqrt(ax*ax + ay*ay + az*az);
  float acc = fabs(mag - 1000.0);

  unsigned long now = millis();

  // Detectar toque
  if (acc > THRESHOLD && (now - lastTap) > COOLDOWN) {
    tapCount++;
    lastTap = now;
    Serial.printf("Toque! Total=%d Estado=%d\n", tapCount, state);

    if (state == IDLE) {
      state = SUIT;
      updateDisplay();
    }
  }

  // Máquina de estados
  if (state == SUIT && (now - lastTap) > PAUSE) {
    if (tapCount >= 1 && tapCount <= 4) {
      suit = tapCount;
      Serial.printf("Palo: %s\n", suitSymbols[suit]);
      tapCount = 0;
      state = NUMBER;
      updateDisplay();
    } else {
      Serial.println("Error: palo inválido");
      reset();
    }
  }

  if (state == NUMBER && (now - lastTap) > PAUSE) {
    if (tapCount >= 1 && tapCount <= 13) {
      number = tapCount;
      Serial.printf("Número: %s\n", numberNames[number]);
      showCard(suit, number);
      sendCard(suit, number);
      delay(3000);
      reset();
    } else {
      Serial.println("Error: número inválido");
      reset();
    }
  }

  delay(20);
}

void reset() {
  state = IDLE;
  tapCount = 0;
  suit = 0;
  number = 0;
  updateDisplay();
}

void updateDisplay() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(10, 40);
  tft.println("RELOJ TOQUES");

  tft.setCursor(10, 80);
  if (state == IDLE) {
    tft.println("Esperando...");
  } else if (state == SUIT) {
    tft.print("Toques: ");
    tft.println(tapCount);
    tft.setCursor(10, 110);
    tft.println("Palo");
  } else if (state == NUMBER) {
    tft.print("Palo: ");
    tft.println(suitSymbols[suit]);
    tft.setCursor(10, 110);
    tft.print("Toques: ");
    tft.println(tapCount);
  }
}

void showCard(uint8_t s, uint8_t n) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  tft.setCursor(20, 60);
  tft.print(numberNames[n]);
  tft.print(" ");
  tft.println(suitSymbols[s]);

  Serial.printf("Mostrando: %s %s\n", numberNames[n], suitSymbols[s]);
}

void sendCard(uint8_t s, uint8_t n) {
  char ssid[64] = "CARD_";

  // Codificar palo
  const char* suitChar[] = {"", CH1, CH2, CH3, CH4};
  strcat(ssid, suitChar[s]);
  strcat(ssid, CH1); // Separador

  // Codificar número (binario)
  for (int i = 3; i >= 0; i--) {
    strcat(ssid, (n >> i) & 1 ? CH2 : CH1);
  }

  Serial.printf("Enviando: %s de %s\n", numberNames[n], suitSymbols[s]);

  // Enviar 100 beacons para asegurar recepción
  for (int i = 0; i < 100; i++) {
    sendBeacon(ssid);
    delay(50);
  }

  Serial.println("Transmisión completada");
}

void sendBeacon(const char* ssid) {
  uint8_t beacon[128] = {0};
  uint8_t mac[6];

  // Frame control
  beacon[0] = 0x80;

  // Destination (broadcast)
  memset(&beacon[4], 0xFF, 6);

  // Source & BSSID
  esp_wifi_get_mac(WIFI_IF_AP, mac);
  memcpy(&beacon[10], mac, 6);
  memcpy(&beacon[16], mac, 6);

  // Beacon interval
  beacon[32] = 0x64;

  // Capability
  beacon[34] = 0x01;

  // SSID
  size_t len = strlen(ssid);
  beacon[36] = 0x00;
  beacon[37] = len;
  memcpy(&beacon[38], ssid, len);

  esp_wifi_80211_tx(WIFI_IF_AP, beacon, 38 + len, false);
}
