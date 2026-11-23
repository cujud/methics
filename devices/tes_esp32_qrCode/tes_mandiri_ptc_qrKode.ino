/*
  ESP32 + TFT 2.8" ILI9341 (TFT_eSPI) + sensor ultrasonic HC-SR04 + buzzer + QR code
  Cara kerja:
    - Saat standby, backlight layar mati dan buzzer diam. QR sudah tergambar di layar.
    - Jika ada objek dalam jarak DETECT_CM, backlight menyala DAN buzzer berbunyi
      (BEEP_COUNT kali bip pendek) sehingga QR terlihat.
    - Layar mati lagi ON_TIME_MS setelah objek terakhir terdeteksi.
  QR memakai library bawaan core ESP32 (esp_qrcode), tanpa library tambahan.
*/
#include <SPI.h>
#include <TFT_eSPI.h>
#include "qrcode.h"          // bawaan core ESP32 (esp_qrcode)

#define ROT 2

// ---- QR ----
#define QR_TEXT "https://contoh.com/absen"   // isi QR, ganti sesuai kebutuhan

// ---- Sensor ultrasonic ----
#define TRIG_PIN 26
#define ECHO_PIN 27
#define DETECT_CM           30
#define ON_TIME_MS       5000UL
#define MEASURE_INTERVAL   100UL
#define CONFIRM_HITS         2

// ---- Buzzer ----
#define BUZZER_PIN       22
#define BUZZER_ON_LEVEL  HIGH       // sesuai hasil tes polaritas buzzer Anda
#define BEEP_COUNT   2
#define BEEP_ON_MS   120
#define BEEP_OFF_MS  100
#define DEBUG_SERIAL 1

#ifndef TFT_BL
#define TFT_BL 32
#endif
#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

TFT_eSPI tft = TFT_eSPI();
bool screenOn = false;
unsigned long lastDetect = 0;
unsigned long lastMeasure = 0;
uint8_t hits = 0;

uint8_t beepsLeft = 0;
bool beepOn = false;
unsigned long beepTimer = 0;

// ---------------------------------------------------------------------------
// Buzzer
// ---------------------------------------------------------------------------
void buzzerInit() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, !BUZZER_ON_LEVEL);   // diam
}

void buzzerWrite(bool on) {
  digitalWrite(BUZZER_PIN, on ? BUZZER_ON_LEVEL : !BUZZER_ON_LEVEL);
}

void startBeep(unsigned long now) {
  beepsLeft = BEEP_COUNT;
  beepOn = false;
  beepTimer = now - BEEP_OFF_MS;
}

void updateBeep(unsigned long now) {
  if (beepsLeft == 0 && !beepOn) return;
  if (!beepOn) {
    if (now - beepTimer >= BEEP_OFF_MS) {
      buzzerWrite(true);
      beepOn = true;
      beepTimer = now;
    }
  } else if (now - beepTimer >= BEEP_ON_MS) {
    buzzerWrite(false);
    beepOn = false;
    beepTimer = now;
    if (beepsLeft > 0) beepsLeft--;
  }
}

// ---------------------------------------------------------------------------
// Layar, QR, sensor
// ---------------------------------------------------------------------------
void setBacklight(bool on) {
  digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
  screenOn = on;
}

// Dipanggil oleh esp_qrcode_generate(): menggambar matriks QR ke TFT
void qrDisplay(esp_qrcode_handle_t qr) {
  int size = esp_qrcode_get_size(qr);
  const int quiet = 4;                         // area putih di sekeliling QR (modul)
  int total = size + quiet * 2;
  int W = tft.width();
  int H = tft.height();
  int scale = min(W, H) / total;               // piksel per modul (bulat)
  if (scale < 1) scale = 1;
  int px = total * scale;
  int x0 = (W - px) / 2;
  int y0 = (H - px) / 2;

  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (esp_qrcode_get_module(qr, x, y)) {
        tft.fillRect(x0 + (x + quiet) * scale, y0 + (y + quiet) * scale,
                     scale, scale, TFT_BLACK);
      }
    }
  }
}

void drawQR(const char* text) {
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qrDisplay;
  cfg.max_qrcode_version = 10;                 // versi terbesar yang boleh dipakai
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  tft.fillScreen(TFT_WHITE);                   // latar putih = area tenang QR
  esp_qrcode_generate(&cfg, text);
}

// cm, atau -1 jika tidak ada pantulan
float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) return -1;
  return duration * 0.0343f / 2.0f;
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  buzzerInit();
  pinMode(TFT_BL, OUTPUT);
  tft.init();
  tft.setRotation(ROT);
  setBacklight(false);
  drawQR(QR_TEXT);
}

void loop() {
  unsigned long now = millis();
  if (now - lastMeasure >= MEASURE_INTERVAL) {
    lastMeasure = now;
    float dist = readDistanceCm();
#if DEBUG_SERIAL
    if (dist > 0) Serial.printf("Jarak: %.1f cm\n", dist);
    else Serial.println("Jarak: tidak ada pantulan");
#endif
    if (dist > 0 && dist <= DETECT_CM) {
      if (hits < 255) hits++;
    } else {
      hits = 0;
    }
    if (hits >= CONFIRM_HITS) {
      lastDetect = now;
      if (!screenOn) {
        setBacklight(true);
        startBeep(now);
#if DEBUG_SERIAL
        Serial.println("Objek terdeteksi -> layar ON + buzzer");
#endif
      }
    }
  }
  updateBeep(now);
  if (screenOn && (now - lastDetect >= ON_TIME_MS)) {
    setBacklight(false);
#if DEBUG_SERIAL
    Serial.println("Timeout -> layar OFF");
#endif
  }
}
