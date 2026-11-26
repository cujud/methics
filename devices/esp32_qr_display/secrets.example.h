// Salin berkas ini menjadi "secrets.h" di folder yang sama, lalu isi nilainya.
// secrets.h di-gitignore: JANGAN commit Wi-Fi password atau DEVICE_TOKEN.
#pragma once

#define WIFI_SSID      "nama-wifi"        // harus Wi-Fi 2.4 GHz
#define WIFI_PASSWORD  "password-wifi"

// Sama persis dengan DEVICE_TOKEN di .env backend (>= 32 karakter).
// Dipakai sebagai Bearer token heartbeat DAN kunci HMAC kode QR.
#define DEVICE_TOKEN   "isi-dengan-DEVICE_TOKEN-dari-.env"

// Opsional (disarankan untuk produksi): sertifikat root CA server dalam
// format PEM agar ESP32 memverifikasi HTTPS. Tanpa ini koneksi memakai
// setInsecure() (terenkripsi, tetapi server tidak diverifikasi).
// #define SERVER_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
