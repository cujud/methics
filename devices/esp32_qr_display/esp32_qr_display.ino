/*
  Layar QR absensi — ESP32 + TFT 2.8" ILI9341 (TFT_eSPI) + HC-SR04 + buzzer

  Cara kerja:
    - ESP32 tersambung Wi-Fi dan menyinkronkan jam lewat NTP (UTC).
    - Setiap 30 detik ESP32 membuat QR baru berisi tautan absen:
        {BASE_URL}/absen?d={DEVICE_ID}&w={window}&c={code}
      window = floor(unix / 30)
      code   = 16 hex pertama HMAC-SHA256(DEVICE_TOKEN, "qr\n" + DEVICE_ID + "\n" + window)
      Algoritma ini harus sama dengan api.QRCode di internal/api/qr.go.
    - Saat standby backlight mati. Bila ada orang dalam DETECT_CM, backlight
      menyala dan buzzer berbunyi sehingga QR terlihat; layar mati lagi
      ON_TIME_MS setelah deteksi terakhir.
    - Heartbeat POST /api/v1/devices/ping tiap PING_INTERVAL_MS agar dashboard
      melihat perangkat online.

  Mahasiswa memindai QR dengan HP, mengisi NIM, dan mengambil swafoto; server
  memverifikasi kode QR dan wajah (lihat README bagian Absen QR).

  Siapkan secrets.h (salin dari secrets.example.h). JANGAN menulis Wi-Fi
  password atau DEVICE_TOKEN langsung di sketch ini.
  QR memakai library bawaan core ESP32 (esp_qrcode), tanpa library tambahan.
*/
#include <SPI.h>
#include <TFT_eSPI.h>
#include "qrcode.h"          // bawaan core ESP32 (esp_qrcode)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "mbedtls/md.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Salin secrets.example.h menjadi secrets.h lalu isi WIFI_SSID, WIFI_PASSWORD, DEVICE_TOKEN."
#endif

// ---- Server & identitas ----
#define BASE_URL         "https://dash.algorithmics.web.id"   // tanpa garis miring di akhir
#define DEVICE_ID        "ESP32-QR-A1"    // harus ada di koleksi devices (seed)
#define HTTP_TIMEOUT_MS  6000
#define PING_INTERVAL_MS 60000UL
#define NTP_SERVER_1     "pool.ntp.org"
#define NTP_SERVER_2     "time.google.com"
#define QR_WINDOW_SEC    30               // sama dengan api.QRWindow

// ---- Layar ----
#define ROT 2                             // 0/2 = portrait 240 x 320

// ---- Sensor ultrasonic ----
#define TRIG_PIN 26
#define ECHO_PIN 27
#define DETECT_CM           30
#define ON_TIME_MS      20000UL           // cukup lama untuk memindai QR
#define MEASURE_INTERVAL   100UL
#define CONFIRM_HITS         2

// ---- Buzzer ----
#define BUZZER_PIN       22
#define BUZZER_ON_LEVEL  HIGH             // sesuai hasil tes polaritas buzzer
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

volatile bool timeOk = false;
long long shownWindow = -1;               // window QR yang sedang tampil
int shownBarSec = -1;
bool shownWaiting = false;

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
// Kode QR (HMAC-SHA256, mbedTLS bawaan core ESP32)
// ---------------------------------------------------------------------------
void qrCodeFor(long long window, char out[17]) {
  static const char* hex = "0123456789abcdef";
  char message[96];
  int len = snprintf(message, sizeof(message), "qr\n%s\n%lld", DEVICE_ID, window);
  uint8_t digest[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const uint8_t*)DEVICE_TOKEN, strlen(DEVICE_TOKEN),
                  (const uint8_t*)message, len, digest);
  for (int i = 0; i < 8; i++) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 0x0F];
  }
  out[16] = '\0';
}

// ---------------------------------------------------------------------------
// Layar
// ---------------------------------------------------------------------------
void setBacklight(bool on) {
  digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
  screenOn = on;
}

// Area QR: persegi di bagian atas layar; sisa tinggi untuk teks & bilah waktu.
int qrAreaSize() { return min(tft.width(), tft.height()); }

// Dipanggil oleh esp_qrcode_generate(): menggambar matriks QR ke TFT
void qrDisplay(esp_qrcode_handle_t qr) {
  int size = esp_qrcode_get_size(qr);
  const int quiet = 4;                         // area putih di sekeliling QR (modul)
  int total = size + quiet * 2;
  int area = qrAreaSize();
  int scale = area / total;                    // piksel per modul (bulat)
  if (scale < 1) scale = 1;
  int px = total * scale;
  int x0 = (tft.width() - px) / 2;
  int y0 = (area - px) / 2;

  tft.fillRect(0, 0, tft.width(), area, TFT_WHITE);
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
  cfg.max_qrcode_version = 10;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, text);
}

void drawCaption() {
  int top = qrAreaSize();
  tft.fillRect(0, top, tft.width(), tft.height() - top, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Pindai untuk absen", tft.width() / 2, top + 8, 4);
  tft.drawString(DEVICE_ID, tft.width() / 2, top + 38, 2);
}

// Bilah sisa waktu window QR saat ini.
void drawTimeBar(int secLeft) {
  int y = tft.height() - 12;
  int w = tft.width() - 20;
  int filled = w * secLeft / QR_WINDOW_SEC;
  tft.fillRect(10, y, w, 6, TFT_LIGHTGREY);
  tft.fillRect(10, y, filled, 6, TFT_DARKGREEN);
}

void drawWaiting(const char* line) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Menyiapkan QR...", tft.width() / 2, tft.height() / 2 - 14, 4);
  tft.drawString(line, tft.width() / 2, tft.height() / 2 + 18, 2);
}

// Gambar ulang QR bila window berganti; perbarui bilah waktu tiap detik.
void updateQR() {
  if (!timeOk) {
    if (!shownWaiting) {
      drawWaiting(WiFi.status() == WL_CONNECTED ? "Sinkronisasi jam (NTP)" : "Menyambung Wi-Fi");
      shownWaiting = true;
    }
    return;
  }
  time_t now = time(nullptr);
  long long window = (long long)now / QR_WINDOW_SEC;
  if (window != shownWindow) {
    char code[17];
    qrCodeFor(window, code);
    char url[192];
    snprintf(url, sizeof(url), "%s/absen?d=%s&w=%lld&c=%s", BASE_URL, DEVICE_ID, window, code);
    if (shownWaiting) tft.fillScreen(TFT_WHITE);
    drawQR(url);
    drawCaption();
    shownWindow = window;
    shownWaiting = false;
    shownBarSec = -1;
#if DEBUG_SERIAL
    Serial.printf("QR baru: %s\n", url);
#endif
  }
  int secLeft = QR_WINDOW_SEC - (int)(now % QR_WINDOW_SEC);
  if (secLeft != shownBarSec) {
    drawTimeBar(secLeft);
    shownBarSec = secLeft;
  }
}

// ---------------------------------------------------------------------------
// Jaringan (task terpisah di core 0 agar sensor & layar tetap responsif)
// ---------------------------------------------------------------------------
bool wifiEnsure(unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

bool syncTime(unsigned long timeoutMs) {
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);   // UTC
  unsigned long t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < timeoutMs) delay(200);
  timeOk = time(nullptr) >= 1700000000;
  Serial.printf("[NTP] %s\n", timeOk ? "tersinkron" : "GAGAL");
  return timeOk;
}

// Heartbeat ke backend. true bila server membalas 2xx.
bool sendPing() {
  String url = String(BASE_URL) + "/api/v1/devices/ping";
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  if (url.startsWith("https")) {
#ifdef SERVER_ROOT_CA
    secure.setCACert(SERVER_ROOT_CA);
#else
    secure.setInsecure();   // tanpa verifikasi sertifikat; isi SERVER_ROOT_CA di secrets.h untuk produksi
#endif
    http.begin(secure, url);
  } else {
    http.begin(plain, url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  http.addHeader("X-Device-Id", DEVICE_ID);
  int code = http.POST(String("device_id=") + DEVICE_ID);
  Serial.printf("[PING] HTTP %d\n", code);
  http.end();
  return code >= 200 && code < 300;
}

void netTask(void*) {
  unsigned long lastPing = 0;
  unsigned long lastNtp = 0;
  bool pinged = false;
  for (;;) {
    if (wifiEnsure(10000)) {
      unsigned long now = millis();
      // Sinkron ulang jam tiap 6 jam agar QR tetap sesuai jam server.
      if (!timeOk || now - lastNtp >= 6UL * 3600UL * 1000UL) {
        if (syncTime(10000)) lastNtp = now;
      }
      if (!pinged || now - lastPing >= PING_INTERVAL_MS) {
        sendPing();
        lastPing = now;
        pinged = true;
      }
    } else {
      Serial.println("[WiFi] gagal tersambung");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ---------------------------------------------------------------------------
// Sensor
// ---------------------------------------------------------------------------
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);          // sambung di latar belakang
  xTaskCreatePinnedToCore(netTask, "net", 8192, NULL, 1, NULL, 0);
}

void loop() {
  unsigned long now = millis();
  if (now - lastMeasure >= MEASURE_INTERVAL) {
    lastMeasure = now;
    float dist = readDistanceCm();
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
        Serial.printf("Objek %.1f cm -> layar ON + buzzer\n", dist);
#endif
      }
    }
  }
  updateBeep(now);
  updateQR();                                     // QR selalu siap walau layar mati
  if (screenOn && (now - lastDetect >= ON_TIME_MS)) {
    setBacklight(false);
#if DEBUG_SERIAL
    Serial.println("Timeout -> layar OFF");
#endif
  }
}
