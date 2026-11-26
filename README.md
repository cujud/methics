![svc by algorithmics](https://proxy-assets.etherthink.xyz/algorithmics/methiccs.webp)
# Algorithmics Face Recognation

Sistem absensi mahasiswa berbasis pengenalan wajah untuk Institut Teknologi
Bacharuddin Jusuf Habibie (ITBJH).

Satu aplikasi Go (halaman server-rendered + htmx dan API JSON) dengan MongoDB,
perangkat ESP8266/ESP32 sebagai pemindai, Face Recognition Service Python
internal dari [repository terpisah](https://git.algorithmics.web.id/hutao/methics-face),
dan (opsional) object storage S3 Garage untuk foto wajah.

Untuk orientasi cepat AI/kontributor berikutnya, mulai dari
[panduan operasional](#operasional-handoff): status, keputusan, batasan, dan prioritas lanjutan.

## Status singkat

| Komponen | Status |
| --- | --- |
| Backend Go + dashboard staf (login, ringkasan, mahasiswa, kelas, laporan, log, akses) | berjalan |
| Administrasi: tambah/hapus mahasiswa, buat/hapus akun dosen | berjalan (administrator) |
| Pengelolaan kelas: tambah/hapus kelas | berjalan (administrator dan dosen; penghapusan diblokir bila kelas masih dipakai) |
| MongoDB (index + seed kelas/perangkat otomatis) | berjalan |
| Firmware perangkat absensi ESP32 + ESP32-CAM (UART) (`devices/esp32_scanner`, `devices/esp32cam_uart`) | tersedia; commissioning perangkat keras wajib sebelum pemasangan |
| Firmware tambahan | alat commissioning perangkat, tidak termasuk runtime server |
| Face Recognition Service Python (repository terpisah, SCRFD + ArcFace) | terhubung ke Go untuk enrollment & scan ([repository layanan](https://git.algorithmics.web.id/hutao/methics-face), [integrasi Face Recognition Service](#integrasi-face-recognition-service)) |
| Penyimpanan foto wajah di Garage (S3) + retensi otomatis | ada, opsional (`S3_*`); tautan foto di halaman Log |
| Operasi aplikasi | production-only |

## Navigasi dokumentasi terpadu

- [Mulai cepat & konfigurasi](#mulai-cepat)
- [Operasional, deployment & handoff](#operasional-handoff)
- [Kontrak API](#kontrak-api)
- [Arsitektur & keputusan teknis](#arsitektur)
- [Panduan kode](#panduan-kode)
- [Database MongoDB](#database)
- [Firmware & perangkat](#perangkat-firmware)
- [Integrasi Face Recognition Service](#integrasi-face-recognition-service)
- [Spesifikasi kebutuhan](#spesifikasi-kebutuhan)
- [Standar penulisan dokumentasi](#standar-dokumentasi)

Semua panduan proyek berada di halaman ini. Source dan deployment Face
Recognition Service tetap berada di [repository terpisah](https://git.algorithmics.web.id/hutao/methics-face).

## Struktur proyek

```text
cmd/server/main.go        entry point: env, routing, middleware, bootstrap akun, shutdown
internal/api/             endpoint JSON, query ber-scope role, logika domain,
                          klien face service (face.go), foto wajah (images.go),
                          fitur administrator (admin.go), scan multi-frame & HMAC (scan.go)
internal/storage/         klien S3 untuk Garage (pustaka standar, SigV4)
internal/controller/      halaman (web.go), fragment htmx (partials.go, admin_partials.go), login (auth.go)
internal/model/           view model halaman
internal/security/        session, principal, CSRF, rate limit login, CSP, recover
internal/store/           koneksi MongoDB, index, seed, akun staf
views/                    template html/template (layouts, pages, partials, fragments)
static/                   CSS, JS, htmx & Lucide yang di-vendor lokal
devices/                  firmware Arduino: esp32_scanner (perangkat absensi), esp32cam_uart (kamera),
                          firmware commissioning perangkat
docs/diagram.excalidraw   diagram arsitektur
```

## Mulai cepat

Kebutuhan deployment: **Go 1.23+** untuk build aplikasi ini, Face Recognition
Service dari [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face),
dan MongoDB produksi dengan autentikasi serta TLS sesuai topologi. Ikuti
dokumentasi masing-masing repository untuk membangun dan menjalankan servicenya.

```powershell
Copy-Item .env.example .env
# Isi semua secret wajib dan URI MongoDB produksi dalam .env.
go mod download
go build -trimpath -ldflags="-s -w" -o bin/methics.exe ./cmd/server
.\bin\methics.exe
```

Sebelum membuka aplikasi, jalankan Face Recognition Service secara terpisah,
pastikan `FACE_SERVICE_URL` dari Go dapat menjangkaunya, dan siapkan MongoDB
serta reverse proxy TLS. Go menolak start bila `APP_ENV` bukan `production`, URI MongoDB
belum diisi, atau secret wajib kurang dari 32 karakter. Akun administrator
pertama dibuat dari `BOOTSTRAP_ADMIN_*`; hapus secret bootstrap setelah akun
berhasil dibuat. Jalankan binary dari direktori yang berisi `views/` dan
`static/`, atau atur `VIEW_DIR`/`STATIC_DIR`.

Nilai acak 32+ karakter bisa dibuat dengan `openssl rand -hex 32` atau di
PowerShell:

```powershell
-join ((48..57)+(65..90)+(97..122) | Get-Random -Count 40 | % {[char]$_})
```

`.env` dimuat saat startup (`godotenv.Overload`) dan menimpa environment proses
dengan nama variabel yang sama. Jika `.env` tidak tersedia, konfigurasi harus
diberikan melalui environment proses. Gunakan berkas tanpa BOM dan batasi
aksesnya hanya untuk akun layanan.

### Build dan deployment

```powershell
go vet ./...
go build -trimpath -ldflags="-s -w" -o bin/methics.exe ./cmd/server
```

Binary membaca `views/` dan `static/` dari direktori kerja (atau dari
`VIEW_DIR`/`STATIC_DIR`); salin kedua folder bersama binary. Terminasi TLS,
HSTS, pembatasan ukuran/rate di edge, serta firewall adalah tanggung jawab
reverse proxy dan infrastruktur deployment.

## Halaman

| Route | Isi | Akses |
| --- | --- | --- |
| `/` | ringkasan absensi hari ini, status perangkat & face service | staf |
| `/mahasiswa` | roster, pendaftaran & penghapusan mahasiswa, enrollment wajah 1–6 foto langsung dari kamera perangkat, lihat foto enrollment | staf (aksi tulis: administrator) |
| `/kelas` | kehadiran dan enrollment per kelas; tambah/hapus kelas | staf (pengelolaan kelas: administrator dan dosen) |
| `/laporan` | rekap status scan hari ini, kehadiran per kelas, 50 scan terbaru, cetak | staf |
| `/logs` | log pemindaian seluruh waktu (200 terbaru) dengan filter status, kelas, pencarian, dan tautan foto capture | staf |
| `/akses` | buat & hapus akun dosen, daftar akun staf, matriks role | administrator |
| `/login` | login staf | publik |
| `/eggs` | halaman putih; ketuk gambar telur untuk memutar musik | publik |

Di halaman Mahasiswa, administrator diminta memberi izin kamera saat menekan
Enroll wajah. Kamera hanya aktif selama dialog enrollment digunakan; browser,
izin per-origin, Permissions-Policy, dan HTTPS dengan sertifikat valid
menentukan akses perangkat.

Dosen hanya melihat kelas yang ditugaskan (`users.class_ids`). Dosen yang
membuat kelas otomatis mendapat penugasan kelas itu; penghapusan hanya tersedia
untuk kelas dalam cakupannya dan ditolak selama masih dipakai mahasiswa,
perangkat, riwayat scan, atau akun staf lain. Filter kelas di Mahasiswa dan Log
diambil dari database. Halaman dirender Go dengan `html/template`; data dimuat
dan disegarkan oleh htmx dari `/partials/*` (bukan SPA/JSON render).

## Konfigurasi

| Variabel | Default | Keterangan |
| --- | --- | --- |
| `APP_ENV` | — | **wajib** bernilai `production`; mode lain ditolak |
| `CSRF_SECRET` | — | **wajib**, ≥ 32 karakter; cookie CSRF selalu `Secure` |
| `DEVICE_TOKEN` | — | **wajib**, ≥ 32 karakter; bearer + kunci HMAC perangkat |
| `MONGODB_URI` | — | **wajib**; koneksi MongoDB produksi dengan autentikasi/TLS sesuai topologi |
| `MONGODB_DATABASE` | `attendance` | nama database (dibuat otomatis saat pertama ditulisi) |
| `FACE_SERVICE_URL` | `http://127.0.0.1:8000` | base URL Face Recognition Service internal (implementasi/deployment: [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face)) |
| `FACE_SIM_THRESHOLD` | `0.45` | ambang cosine similarity untuk menyatakan wajah cocok |
| `FACE_POSE_THRESHOLD` | `0.30` | ambang kemiripan antar-foto dalam satu enrollment (1–6 pose) |
| `SCAN_MIN_MATCH_FRAMES` | `2` | frame scan yang harus cocok dengan mahasiswa yang sama agar langsung Hadir |
| `FACE_SERVICE_TIMEOUT_SECONDS` | `15` | batas waktu `/embed`; enrollment dihitung per gelombang inferensi (maks. 2 foto paralel; 6 foto = hingga 45 detik) |
| `FACE_SERVICE_TOKEN` | — | **wajib**, ≥ 32 karakter, sama di Go dan Face Recognition Service; layanan menolak request tanpa bearer valid |
| `FACE_SERVICE_BIND_ADDRESS` | `127.0.0.1` | opsional, untuk pengaturan binding deployment Face Recognition Service bila memakai variabel bersama; bukan konfigurasi yang dibaca aplikasi Go |
| `S3_ENDPOINT`, `S3_REGION`, `S3_BUCKET` | kosong, `garage`, kosong | object storage S3 (Garage) untuk foto wajah; kosong = foto tidak disimpan |
| `S3_ACCESS_KEY_ID`, `S3_SECRET_ACCESS_KEY` | kosong | kunci Garage khusus bucket (bukan kunci admin) |
| `IMAGE_RETENTION_DAYS` | `30` | masa simpan foto scan berstatus `Hadir`; `0` = tanpa batas |
| `IMAGE_REVIEW_RETENTION_DAYS` | `90` | masa simpan foto scan `Perlu ditinjau`/`Ditolak`; `0` = tanpa batas |
| `HTTP_ADDR` | `:8080` | alamat listen |
| `STATIC_DIR` | `static` | lokasi aset |
| `VIEW_DIR` | `views` | lokasi template |
| `BOOTSTRAP_ADMIN_USERNAME`, `BOOTSTRAP_ADMIN_PASSWORD` | — | wajib saat belum ada administrator aktif; password ≥ 12 karakter |
| `BOOTSTRAP_LECTURER_USERNAME`, `BOOTSTRAP_LECTURER_PASSWORD` | — | opsional; akun dosen dibuat bila username belum ada (akun dosen lain dibuat di `/akses`) |
| `BOOTSTRAP_LECTURER_CLASSES` | `IK24-A,IK24-B` | kelas untuk akun dosen bootstrap |

Server menolak start bila `APP_ENV` tidak persis `production`, secret wajib
kurang dari 32 karakter, `MONGODB_URI` kosong, MongoDB tidak dapat dihubungi
dalam 8 detik, atau belum ada administrator dan kredensial bootstrap tidak
diisi. `BOOTSTRAP_RESET_PASSWORDS=true` tidak didukung. Hapus secret bootstrap
setelah akun terbentuk. Templat lengkap ada di
[.env.example](.env.example).

Ambang deteksi (`CONF_THRESHOLD`) dan pilihan model diatur di Face Recognition
Service; lihat konfigurasi pada [repository layanan](https://git.algorithmics.web.id/hutao/methics-face).
Ambang pencocokan wajah (`FACE_SIM_THRESHOLD`) diatur di Go karena pencocokan
dilakukan oleh Go.

## Keamanan akun staf

- Tidak ada kredensial demo bawaan; akun staf dibuat dari `BOOTSTRAP_*`.
- Password disimpan dengan bcrypt; login dibatasi 10 percobaan per IP per
  5 menit.
- Session cookie HttpOnly/SameSite/Secure (`itbjh_session`, 8 jam, idle
  30 menit) dan CSRF token (`itbjh_csrf`).
- Session store dan login limiter masih **in-memory**: restart mengakhiri
  semua sesi dan deployment multi-instance perlu store bersama.
- Mahasiswa adalah record roster/subjek absensi tanpa akun aplikasi.
- Administrator membuat akun dosen (username, nama, password, kelas yang
  diampu) dan menghapusnya di halaman `/akses`; akun administrator tidak bisa
  dihapus dari sana. Mengubah kelas dosen, reset password, dan menonaktifkan
  akun belum ada di UI; untuk sementara gunakan `mongosh` (contoh di
  [panduan database](#database) §6).
- Password minimal 12 karakter untuk semua akun.

## Perangkat

Perangkat absensi = **ESP32 utama** (`devices/esp32_scanner`: ultrasonik, layar
TFT, buzzer, Wi-Fi) + **ESP32-CAM** (`devices/esp32cam_uart`) yang tersambung
lewat kabel UART. Kamera mati saat idle; begitu sensor ultrasonik mendeteksi
orang, ESP32 menyalakan kamera, mengambil ±4 frame selama ±2 detik, lalu
mengirim semuanya ke `POST /api/v1/scans` (token + HMAC + jam NTP). Wajah yang
cocok di ≥ 2 frame langsung tercatat **Hadir** di kelas mahasiswa itu; wajah
yang tidak cocok dengan siapa pun tampil **"Wajah tidak Terdaftar"** di layar
TFT. Tiap 15 detik ESP32 juga mengirim heartbeat ke
`POST /api/v1/devices/ping`. Wiring, konfigurasi, urutan upload, dan
commissioning ada di [panduan perangkat](#perangkat-firmware) §2. Kamera Wi-Fi
alternatif hanya untuk commissioning; pemasangan permanen memakai UART.

Wi-Fi dan `DEVICE_TOKEN` untuk ESP32 disimpan di
`devices/esp32_scanner/secrets.h` (salin dari `secrets.example.h`; di-gitignore).

> **Kredensial perangkat sudah ter-commit.** Versi lama sketch ESP8266 menulis
> password Wi-Fi dan `DEVICE_TOKEN` langsung di kode dan sudah masuk riwayat
> Git. Ganti token dan password itu ([panduan perangkat](#perangkat-firmware) §4).

## Face service

Face Recognition Service dikembangkan dan di-deploy dari repository
[hutao/methics-face](https://git.algorithmics.web.id/hutao/methics-face), bukan
dari folder dalam repository ini. Service bersifat **stateless**: Go memanggil
`POST /register` satu request per foto secara paralel saat enrollment dan
`POST /embed` saat scan, lalu Go sendiri menyimpan embedding ke MongoDB dan
mencocokkannya dengan cosine similarity terhadap semua pose. Dashboard dan
perangkat ESP32 hanya memakai API Go; service Python harus tetap berada di
jaringan internal dan memakai `FACE_SERVICE_TOKEN` yang sama dengan Go.

Detail kontrak Go ada di [integrasi Face Recognition Service](#integrasi-face-recognition-service).
Instruksi source, konfigurasi runtime, build, dan deployment Python ada di
[repository Face Recognition Service](https://git.algorithmics.web.id/hutao/methics-face).

## Penyimpanan foto (Garage)

Opsional. Bila `S3_*` diisi, Go menyimpan foto enrollment dan foto capture
scan di bucket Garage privat; MongoDB hanya menyimpan kuncinya. Foto dipakai
untuk peninjauan, audit, dan enrollment ulang saat model diganti — pencocokan
tetap memakai embedding. Foto enrollment bisa dibuka dari tombol **Lihat
foto** di halaman Mahasiswa; foto scan dari kolom **Foto** di halaman Log.
Foto scan dihapus otomatis setelah
`IMAGE_RETENTION_DAYS`/`IMAGE_REVIEW_RETENTION_DAYS`. Bila Garage mati,
absensi tetap berjalan tanpa foto.

```bash
# di server Garage
garage bucket create methics-faces
garage key create methics-app
garage bucket allow --read --write methics-faces --key methics-app
```

Isi Key ID dan Secret ke `.env`, lalu restart server; log start harus
menulis `image storage: bucket "methics-faces" di … siap`. Detail di
[integrasi Face Recognition Service](#integrasi-face-recognition-service) §8.

Embedding dan foto adalah data biometrik sensitif: aktifkan enkripsi
disk/backup, batasi akses database dan bucket, dokumentasikan persetujuan
(consent) dan retensi, serta sediakan penghapusan data.

## API

Detail di [kontrak API](#kontrak-api). Ringkasan:

- `GET /api/v1/health` — publik
- `GET /api/v1/dashboard`, `/classes`, `/reports` — session staf
- `POST /api/v1/students` — administrator + CSRF (JSON)
- `POST /api/v1/students/{nim}/face-template` — administrator + CSRF (multipart `images`, 1–6 foto JPEG dari kamera browser; halaman kamera memerlukan HTTPS atau localhost)
- `DELETE /api/v1/students/{nim}` — administrator + CSRF (hapus mahasiswa + data wajah, foto, riwayat scan)
- `GET|POST /api/v1/staff`, `DELETE /api/v1/staff/{id}` — administrator + CSRF (akun dosen)
- `POST /api/v1/scans` — perangkat (multipart `device_id`, `class_id` opsional, `captured_at`, `liveness_passed`, 1–6 `image`; bearer + HMAC)
- `GET|POST /api/v1/devices/ping` — heartbeat perangkat
- `GET /api/v1/images/{key...}` — foto enrollment/scan dari Garage (session staf; dosen hanya kelasnya)

HMAC dan timestamp melindungi integritas request dan membantu menolak replay;
keduanya tidak membuktikan bahwa wajah hidup. Implementasi ini belum melakukan
liveness detection, dan tidak boleh dipakai untuk keputusan kehadiran berisiko
tinggi tanpa mekanisme anti-spoof tervalidasi, persetujuan yang sesuai, serta
peninjauan manusia untuk hasil tidak pasti.

## Panduan Hardening MongoDB Menuju Production

MongoDB produksi wajib menggunakan autentikasi dan konfigurasi jaringan yang
dibatasi. Terapkan langkah-langkah hardening berikut sebelum layanan digunakan:

1. **Buat User Otentikasi Khusus Aplikasi:**
   Buka terminal di server MongoDB (`mongosh`), pindah ke database `admin`, dan buat akun aplikasi dengan izin `readWrite` hanya pada database aplikasi (nilai `MONGODB_DATABASE`; contoh di bawah memakai `attendance`):
   ```javascript
   use admin
   db.createUser({
     user: "methics_app",
     pwd: "GantiDenganPasswordSangatKuatDanPanjang123!",
     roles: [
       { role: "readWrite", db: "attendance" }
     ]
   })
   ```

2. **Aktifkan Autentikasi di Konfigurasi MongoDB (`/etc/mongod.conf`):**
   Pastikan blok `security.authorization` disetel ke `enabled`:
   ```yaml
   security:
     authorization: enabled
   ```

3. **Batasi Network Binding & Firewall:**
   - Jangan membuka MongoDB ke internet publik (`0.0.0.0`). Setel `net.bindIp` hanya ke IP private/internal atau VPN:
     ```yaml
     net:
       port: 27017
       bindIp: 127.0.0.1,10.100.1.11
     ```
   - Gunakan firewall Ubuntu (`ufw`) agar hanya server backend Go (dan face service) yang diizinkan mengakses port 27017:
     ```bash
     sudo ufw allow from <IP_SERVER_GO> to any port 27017 proto tcp
     ```

4. **Perbarui Connection String di environment aplikasi:**
   Sesuaikan URI di environment file server backend:
   ```env
   MONGODB_URI=mongodb://<user>:<password>@<host>:27017/attendance?authSource=admin
   MONGODB_DATABASE=attendance
   ```
   URI koneksi tidak dicatat ke log aplikasi. Batasi akses environment file
   dan log server hanya untuk operator berwenang.

5. **Restart Service MongoDB:**
   ```bash
   sudo systemctl restart mongod
   ```

6. **Enkripsi In-Transit (TLS/SSL) & Backup:**
   - Bila MongoDB dan server Go berjalan pada server fisik yang terpisah melewati jaringan umum, aktifkan TLS pada MongoDB (`net.tls.mode: requireTLS`).
   - Jadwalkan backup berkala menggunakan utilitas `mongodump` dan simpan arsip backup secara aman (contoh perintah di [panduan database](#database) §7).

## Checklist sebelum production

- [ ] `APP_ENV=production`, semua secret acak ≥ 32 karakter, secret bootstrap dihapus.
- [ ] TLS di reverse proxy (HSTS belum dipasang oleh aplikasi).
- [ ] MongoDB di-hardening sesuai panduan di atas, backup terenkripsi terjadwal.
- [ ] Zona waktu server = zona waktu kampus (batas "hari ini" mengikuti jam server).
- [ ] Face service berjalan di jaringan internal dengan `FACE_SERVICE_TOKEN`.
- [ ] Kredensial perangkat tidak ada di Git; `DEVICE_TOKEN` diganti bila pernah bocor.
- [ ] Heartbeat memakai `DEVICE_TOKEN`; `/api/v1/health` hanya read-only.
- [ ] Session store bersama bila lebih dari satu instance.
- [ ] Bila foto disimpan: bucket Garage privat, kunci khusus bucket (bukan `admin_token`), port RPC 3901 dibatasi, retensi disetujui institusi.

## Pemecahan masalah

| Gejala | Penyebab / solusi |
| --- | --- |
| `CSRF_SECRET must contain at least 32 characters` | `.env` belum terisi/terbaca (cek BOM, cek direktori kerja) |
| `initialize database (...): ... ping mongodb` | MongoDB tidak terjangkau; cek `MONGODB_URI`, firewall, `bindIp` |
| `first startup requires BOOTSTRAP_ADMIN_USERNAME ...` | belum ada administrator aktif; isi variabel bootstrap |
| Login `429` | lebih dari 10 percobaan dari IP yang sama dalam 5 menit; tunggu |
| Semua pengguna ter-logout | server di-restart (session in-memory) |
| "Verifikasi Python" offline | face service belum berjalan, model belum selesai dimuat, atau `FACE_SERVICE_URL` salah (default port `8000`) |
| Enrollment: "Tidak ada wajah yang terdeteksi (foto ke-N)" | foto ke-N buram/terlalu gelap/wajah terlalu kecil; ganti foto itu |
| Enrollment: "Wajah (foto ke-N) tidak cocok dengan foto pertama" | foto ke-N orang lain, atau pose terlalu ekstrem (> ~45°); kurangi sudut atau buang foto itu (ambang `FACE_POSE_THRESHOLD`) |
| Enrollment banyak foto gagal / notifikasi hilang | Go menyimpan semua embedding sekaligus hanya setelah seluruh foto berhasil; satu foto gagal membatalkan batch. Saat request bermasalah, UI sekarang menampilkan indikator/toast. Periksa log Go `enrollment … gagal (N foto, durasi)` dan log Python `Registration request received` / `Registration completed` untuk memastikan tiap request sampai ke Face Service. Jika koneksi terputus, muat ulang roster untuk memeriksa status template sebelum mencoba lagi. |
| Enrollment: "Wajah … sudah terdaftar atas NIM …" | wajah mirip (≥ `FACE_SIM_THRESHOLD`) dengan template mahasiswa lain |
| Enrollment / scan: `face_service_error` setelah memperbarui Face Recognition Service | Pastikan versi layanan masih mendukung kontrak `/health`, `/embed`, dan `/register` yang dipakai Go; lihat [integrasi Face Recognition Service](#integrasi-face-recognition-service) dan [repository layanan](https://git.algorithmics.web.id/hutao/methics-face) |
| Perangkat "offline" | tidak ada ping/scan dalam 2 menit; lihat [panduan perangkat](#perangkat-firmware) §8 |
| Log: `image storage: PERINGATAN bucket … belum bisa diakses` | Garage tidak terjangkau dari server Go, bucket belum dibuat, atau `S3_ACCESS_KEY_ID`/`S3_SECRET_ACCESS_KEY` salah |
| Kolom Foto di Log berisi `-` | penyimpanan foto nonaktif, unggah gagal (lihat log `image storage: upload … gagal`), scan dibuat sebelum `S3_*` diisi/server di-restart, atau foto sudah lewat retensi |
| `image storage: S3 endpoint, bucket, access key, dan secret wajib diisi` | `S3_*` terisi sebagian; lengkapi semua atau kosongkan semua |
| Perubahan `.env` tidak berpengaruh | pastikan binary direstart dan `.env` berada di working directory; nilai di `.env` menimpa environment proses |
| Login gagal setelah `BOOTSTRAP_ADMIN_PASSWORD` diubah | bootstrap hanya membuat administrator jika belum ada; lakukan rotasi kredensial akun aktif melalui prosedur operator database yang disetujui |
| Mahasiswa contoh lama (`IK24001`–`IK24005`) masih tampil | sisa seed versi lama di MongoDB; seed tidak membuatnya lagi, jadi hapus sekali lewat tombol hapus di halaman Mahasiswa |
| `NIM harus 9 digit angka` | NIM wajib 9 digit angka, mis. `241011001` |
| Face Recognition Service gagal start | ikuti troubleshooting dan periksa log dari deployment pada [repository layanan](https://git.algorithmics.web.id/hutao/methics-face); pastikan `FACE_SERVICE_TOKEN` minimal 32 karakter, jaringan model tersedia, dan `FACE_SERVICE_URL` Go benar |

## Lisensi

Lihat [LICENSE](LICENSE). Lisensi pustaka pihak ketiga yang di-vendor ada di
`static/js/vendor/*.LICENSE.txt`.


## Operasional & Handoff

### Handoff Operasional — Algorithmics Face Recognation

Dokumen ringkas untuk operator dan engineer yang melanjutkan pemeliharaan
aplikasi absensi wajah ITBJH. Produk menyediakan dashboard staf, backend Go,
Face Recognition Service internal berbasis Python dari
[repository terpisah](https://git.algorithmics.web.id/hutao/methics-face),
penyimpanan MongoDB, dan firmware perangkat absensi.

#### Status dan Batasan Deployment

Aplikasi hanya mendukung konfigurasi production. Sebelum menyalakan layanan:

- Set `APP_ENV=production`.
- Isi `MONGODB_URI`; URI tidak boleh kosong.
- Isi `CSRF_SECRET`, `DEVICE_TOKEN`, dan `FACE_SERVICE_TOKEN` dengan secret
  acak minimal 32 karakter. Go dan Face Recognition Service harus memakai
  nilai `FACE_SERVICE_TOKEN` yang sama.
- Siapkan akun administrator awal memakai `BOOTSTRAP_ADMIN_USERNAME` dan
  `BOOTSTRAP_ADMIN_PASSWORD`. Bootstrap hanya membuat akun bila belum ada
  administrator aktif. `BOOTSTRAP_RESET_PASSWORDS` tidak didukung.
- Deploy Face Recognition Service mengikuti dokumentasi
  [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face).
  Terminasi HTTPS pada reverse proxy dan batasi jaringan service ke backend.
  Service memerlukan bearer token pada setiap endpoint; jangan mengekspos
  port-nya ke jaringan publik.
- Semua heartbeat perangkat wajib memakai `DEVICE_TOKEN`. Health endpoint
  bersifat read-only dan tidak mendaftarkan atau memperbarui perangkat.

Rincian konfigurasi dan langkah deployment terdapat di [panduan utama](#mulai-cepat).
Contoh variabel ada di [.env.example](.env.example); jangan menyimpan secret
produksi ke Git.

#### Fungsi yang Tersedia

- Login staf berbasis session, role administrator/dosen, CSRF, rate limit
  login, dan pembatasan data dosen menurut kelas.
- Dashboard, roster mahasiswa, kelas, laporan, log scan, pengelolaan akun
  dosen, dan enrollment 1–6 foto per mahasiswa.
- Backend Go memanggil Python untuk embedding; Go menyimpan template ke
  MongoDB dan melakukan pencocokan cosine.
- Perangkat mengirim scan multi-frame dengan bearer token dan HMAC-SHA256.
  Heartbeat menggunakan bearer token.
- Foto enrollment dan bukti scan dapat disimpan secara opsional di Garage
  melalui konfigurasi S3.

#### Arsitektur dan Dokumen Rujukan

| Area | Rujukan |
| --- | --- |
| Instalasi, environment, operasi dan checklist | [panduan utama](#mulai-cepat) |
| Endpoint HTTP dan autentikasi | [kontrak API](#kontrak-api) |
| Komponen, alur data, dan konfigurasi | [arsitektur](#arsitektur) |
| Koleksi, index, dan operasi MongoDB | [panduan database](#database) |
| Firmware absensi dan commissioning perangkat | [panduan perangkat](#perangkat-firmware) |
| Kontrak integrasi face service | [integrasi Face Recognition Service](#integrasi-face-recognition-service) |
| Source dan deployment Face Recognition Service | [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face) |
| Struktur dan tanggung jawab kode | [panduan kode](#panduan-kode) |
| Kebutuhan produk dan status implementasi | [spesifikasi kebutuhan](#spesifikasi-kebutuhan) |

Alur request utama:

1. Staf masuk melalui backend Go dan memakai dashboard HTML/htmx.
2. Administrator mendaftarkan mahasiswa dan mengambil foto enrollment; Go
   meneruskan foto ke Face Recognition Service `/register`.
3. ESP32 utama mengambil frame dari ESP32-CAM melalui UART dan mengirim scan
   bertanda tangan ke Go.
4. Go meneruskan frame ke Face Recognition Service `/embed`, membandingkan embedding dengan
   template di MongoDB, lalu menyimpan hasil scan.

#### Perangkat dan Commissioning

Firmware utama untuk pemakaian absensi adalah `devices/esp32_scanner` bersama
`devices/esp32cam_uart`. Commissioning kabel, catu daya, kamera, token, dan
hasil scan wajib dilakukan sebelum pemasangan.

Firmware ESP32-CAM Wi-Fi, kamera stream, ESP8266 heartbeat, serta sketch
ultrasonik/buzzer adalah alat commissioning/diagnostik, bukan konfigurasi
runtime produksi. Mode kamera Wi-Fi memakai HTTP tanpa TLS dan tidak untuk
jaringan produksi. Lihat [panduan perangkat](#perangkat-firmware) untuk prosedur
dan batasan keamanan perangkat.

Konsensus beberapa frame bukan liveness detection. Foto atau video replay
masih dapat lolos; jangan menganggap hasil identifikasi sebagai bukti
anti-spoof. Ambang kecocokan juga perlu dikalibrasi dengan data yang sah dan
persetujuan institusi sebelum penggunaan operasional.

#### Operasi dan Risiko yang Belum Dituntaskan

- Test otomatis sengaja tidak disertakan dalam repository. Jalankan build dan
  pemeriksaan statis sebelum rilis; lakukan commissioning perangkat dan
  verifikasi deployment sesuai prosedur operasional.
- Session store dan login rate limiter berada di memori proses; multi-instance
  memerlukan penyimpanan bersama.
- HTTPS/TLS, hardening MongoDB, enkripsi serta pengujian backup, rotasi token
  perangkat, persetujuan biometrik, dan kebijakan retensi harus ditetapkan
  operator/institusi.
- Penghapusan mahasiswa menghapus template, foto terkait, dan riwayat scan;
  pastikan proses penghapusan sesuai kebijakan penyimpanan/backup institusi.
- Template hasil model recognition yang berbeda tidak dijamin sebanding;
  rencanakan enrollment ulang bila model recognition diganti.

Sebelum perubahan, periksa `git status` dan pertahankan perubahan lokal yang
tidak terkait. Jangan membaca, menyalin, atau mengubah `.env` lokal tanpa
permintaan eksplisit.


## Kontrak API

### Kontrak API Algorithmics Face Recognation

Acuan: `cmd/server/main.go` (routing), `internal/api/server.go` (handler JSON),
`internal/api/admin.go` (hapus mahasiswa, akun dosen), `internal/api/images.go`
(foto), `internal/controller/partials.go` dan `admin_partials.go` (fragment htmx). Bila dokumen ini berbeda
dengan kode, **kode yang berlaku** — lalu perbarui dokumen ini.

Go adalah satu-satunya pintu masuk untuk browser dan perangkat. Face service
Python bersifat internal di belakang Go; source dan deployment dipelihara di
[repository methics-face](https://git.algorithmics.web.id/hutao/methics-face),
sedangkan kontrak Go-nya dijelaskan di [integrasi Face Recognition Service](#integrasi-face-recognition-service).

#### 1. Peta endpoint

| Kelompok | Method & path | Auth | Respons |
| --- | --- | --- | --- |
| Halaman | `GET /`, `/mahasiswa`, `/kelas`, `/laporan`, `/logs` | session staf | HTML |
| Halaman | `GET /akses` | session administrator (dosen dialihkan ke `/`) | HTML |
| Auth | `GET /login`, `POST /login`, `POST /logout` | — / CSRF | HTML / redirect |
| Easter egg | `GET /eggs` | publik | halaman putih; ketuk gambar telur untuk memulai audio dari `/static/media/` |
| Fragment | `GET/POST/DELETE /partials/*` (§5) | session staf (+ CSRF untuk POST/DELETE) | HTML |
| API staf | `GET /api/v1/dashboard`, `/classes`, `/reports` | session staf | JSON |
| API staf | `POST /api/v1/students`, `/students/{nim}/face-template` | session administrator + CSRF | JSON |
| API staf | `DELETE /api/v1/students/{nim}`, `GET\|POST /api/v1/staff`, `DELETE /api/v1/staff/{id}` | session administrator + CSRF | JSON |
| API staf | `GET /api/v1/images/{key...}` | session staf (dosen: hanya kelasnya) | gambar |
| Perangkat | `POST /api/v1/scans` | bearer `DEVICE_TOKEN` + HMAC + timestamp | JSON |
| Perangkat | `GET\|POST /api/v1/devices/ping` | bearer `DEVICE_TOKEN` | JSON |
| Publik | `GET /api/v1/health` | — | JSON |
| Aset | `GET /static/*` | — | file |

Halaman server-rendered **tidak** membaca `/api/v1`; halaman memuat datanya
dari `/partials/*`. Endpoint `/api/v1` adalah API JSON untuk integrasi dan
perangkat. Keduanya memakai query & validasi yang sama di
`internal/api/server.go`, jadi datanya konsisten.

#### 2. Autentikasi

##### Staf (browser)

- Login `POST /login` (form `username`, `password`, `next`, `csrf_token`).
  Berhasil → session token diperbarui, redirect `303` ke `next` (hanya path
  lokal; selain itu ke `/`). Gagal → halaman login dengan pesan.
- Rate limit login: **10 percobaan per IP per 5 menit**; lebih dari itu
  `429` + header `Retry-After: 300`.
- Session cookie `itbjh_session`: HttpOnly, SameSite=Lax, umur 8 jam, idle
  30 menit, selalu `Secure`. Disimpan **in-memory**.
- CSRF (gorilla/csrf, cookie `itbjh_csrf`) berlaku untuk semua method tidak
  aman di jalur staf, termasuk `POST`/`DELETE /api/v1/*` staf. Kirim lewat field form
  `csrf_token` atau header `X-CSRF-Token`. Token tersedia di
  `<meta name="csrf-token">` pada halaman. Token salah → `403` (teks biasa).
- Role dan kelas dosen dibaca ulang dari MongoDB di setiap request
  (`users.role`, `users.class_ids`). Role dari query/header klien tidak
  pernah dipakai. Akun yang dihapus atau dinonaktifkan langsung kehilangan
  sesinya pada request berikutnya.

##### Perangkat

Lihat §4 dan [panduan perangkat](#perangkat-firmware). Endpoint perangkat
dipasang di luar session/CSRF.

#### 3. Format error JSON

Semua error dari `/api/v1/*` berbentuk:

```json
{ "error": { "code": "invalid_signature", "message": "Signature scan tidak valid atau timestamp kedaluwarsa." } }
```

| HTTP | `code` | Sumber |
| --- | --- | --- |
| 401 | `unauthorized` | session tidak ada/kedaluwarsa (API staf & `/partials/*`) |
| 403 | `forbidden` | role tidak diizinkan (mis. dosen memanggil `POST /api/v1/students`) |
| 400 | `invalid_json` | body JSON rusak / > 1 MiB |
| 400 | `missing_fields` | field wajib kosong |
| 400 | `invalid_nim` | NIM bukan 9 digit angka (format `241011001`) |
| 400 | `invalid_class`, `unknown_class` | kelas tidak ada |
| 409 | `student_exists` | NIM sudah terdaftar |
| 404 | `student_not_found` | NIM tidak ditemukan (enrollment / hapus) |
| 400 | `invalid_input` | data akun dosen tidak valid (username, password, kelas) |
| 409 | `username_taken` | username akun sudah dipakai |
| 403 | `not_lecturer` | mencoba menghapus akun administrator lewat `/api/v1/staff/{id}` |
| 404 | `user_not_found` | akun staf tidak ditemukan |
| 422 | `no_face` | salah satu foto enrollment tidak berisi wajah |
| 422 | `inconsistent_faces` | foto-foto enrollment bukan orang yang sama |
| 409 | `face_already_enrolled` | wajah pada foto enrollment sudah terdaftar atas NIM lain |
| 400 | `invalid_image` | bukan multipart, field `image` hilang, kosong, atau > 8 MiB |
| 401 | `invalid_device` | bearer token perangkat salah (scan) |
| 401 | `invalid_token` | bearer token perangkat salah (ping) |
| 401 | `invalid_signature` | HMAC salah atau `X-Device-Timestamp` > ±2 menit |
| 400 | `stale_capture` | `captured_at` bukan RFC3339 atau > ±5 menit |
| 400 | `invalid_liveness` | `liveness_passed` bukan `true`/`false` |
| 403 | `unknown_device` | `device_id` tidak ada di koleksi `devices` |
| 502 | `face_service_error` | face service gagal/timeout (`FACE_SERVICE_TIMEOUT_SECONDS`, default 15 detik) |
| 404 | `image_not_found`, `image_storage_disabled` | foto tidak tercatat/sudah dihapus, atau `S3_*` kosong |
| 502 | `image_storage_error` | Garage tidak dapat dihubungi saat mengambil foto |
| 500 | `database_error`, `enroll_error` | kegagalan internal |

#### 4. API JSON

##### `GET /api/v1/health` — publik

```json
{
  "status": "ok",
  "faceVerification": {
    "online": true,
    "provider": "Python face service",
    "detectionModel": "face_detection/scrfd_10g.onnx",
    "recognitionModel": "face_recognition/arcface_r50_fp16.onnx"
  },
  "imageStorage": { "enabled": true, "online": true }
}
```

`imageStorage.enabled` = `S3_*` terisi; `online` = `HEAD` bucket berhasil
dalam 3 detik.

`faceVerification.online` = face service menjawab `GET /health` dalam
3 detik dengan `200` dan `model_loaded: true`; Go mengautentikasi request
internal tersebut dengan `FACE_SERVICE_TOKEN`. Bila tidak terjangkau:
`{"online": false, "provider": "Python face service"}`. Endpoint health
bersifat read-only dan tidak mengubah status perangkat.

##### Manajemen model wajah — administrator + CSRF

Go meneruskan API manajemen model Python tanpa mengekspos face service ke
jaringan publik:

| Go | Face service | Penggunaan |
| --- | --- | --- |
| `GET /api/v1/face/models?refresh_remote=true` | `GET /models` | katalog Hugging Face dan status model lokal |
| `POST /api/v1/face/models/download` | `POST /models/download` | JSON `{"model_id":"face_detection/scrfd_10g.onnx","force":false}` |
| `POST /api/v1/face/models/switch` | `POST /models/switch` | JSON berisi satu atau beberapa `det_model`, `rec_model`, `conf_threshold`, `iou_threshold` |

Respons JSON dan status layanan Python diteruskan apa adanya. Request harus
berupa JSON valid; timeout operasi model di Go adalah 10 menit. Mengganti
model pengenalan membuat embedding lama tidak sebanding; lakukan enrollment
ulang untuk mahasiswa sebelum scan memakai model baru.

##### `GET /api/v1/dashboard` — staf

Administrator menerima semua kelas; dosen hanya `users.class_ids`. Data
"hari ini" mengikuti zona waktu server.

```json
{
  "summary": { "students": 5, "classes": 3, "verifiedScans": 3, "scansToReview": 0, "faceTemplates": 2 },
  "classes": [
    { "id": "IK24-A", "name": "Ilmu Komputer A", "present": 2, "studentCount": 2 }
  ],
  "integrations": {
    "esp32": { "online": true, "lastSeen": "08:11" },
    "faceVerification": { "online": true, "provider": "Python face service", "detectionModel": "…", "recognitionModel": "…" }
  },
  "attendance": [
    {
      "student": { "id": "student-241011001", "nim": "241011001", "name": "Alya Putri" },
      "class": { "id": "IK24-A", "name": "Ilmu Komputer A" },
      "scannedAt": "2026-10-21T00:02:00Z",
      "faceVerified": true,
      "faceEnrolled": true,
      "faceTemplates": 6,
      "faceImageUrl": "/api/v1/images/enroll/student-241011001/20261028T080000Z-3f9a1c2b4d5e.jpg",
      "faceImageUrls": ["/api/v1/images/enroll/student-241011001/20261028T080000Z-3f9a1c2b4d5e.jpg", "…"],
      "deviceId": "ESP32-GED-A1",
      "status": "Hadir"
    }
  ]
}
```

- `attendance` = seluruh roster dalam cakupan + scan **terakhir hari ini**
  per mahasiswa. Belum ada scan → `scannedAt: null`, `status: "Belum hadir"`.
- `scannedAt` berupa RFC3339 UTC; `esp32.lastSeen` berupa jam lokal `HH:MM`
  (kosong bila belum pernah terlihat). `esp32.online` = `last_seen` < 2 menit.

##### `GET /api/v1/classes` — staf

```json
{
  "summary": { "classes": 3, "students": 5, "enrolled": 2, "present": 1 },
  "classes": [
    { "id": "IK24-A", "name": "Ilmu Komputer A", "studentCount": 2, "enrolled": 1, "present": 1, "attendanceRate": 50 }
  ]
}
```

`present` = jumlah mahasiswa unik berstatus `Hadir` hari ini;
`attendanceRate` = `present * 100 / studentCount` (dibulatkan ke bawah).

##### `GET /api/v1/reports` — staf

```json
{
  "summary": { "verifiedScans": 1, "reviewScans": 0, "rejectedScans": 0, "students": 5, "present": 1, "attendanceRate": 20 },
  "statusBreakdown": [ { "status": "Hadir", "count": 1 } ],
  "classes": [ { "id": "IK24-A", "name": "Ilmu Komputer A", "studentCount": 2, "enrolled": 1, "present": 1, "attendanceRate": 50 } ],
  "events": [
    {
      "student": { "nim": "241011001", "name": "Alya Putri" },
      "class": { "id": "IK24-A", "name": "Ilmu Komputer A" },
      "scannedAt": "2026-10-21T00:02:00Z",
      "status": "Hadir",
      "confidence": 0.91,
      "reviewReason": "",
      "deviceId": "ESP32-GED-A1",
      "imageUrl": "/api/v1/images/scans/2026/10/21/000200Z-ESP32-GED-A1-3f9a1c2b4d5e.jpg"
    }
  ]
}
```

`imageUrl` kosong bila foto tidak disimpan atau sudah lewat masa retensi.
`summary` dan `statusBreakdown` dihitung untuk **hari ini**; `events` adalah
**50 event terbaru tanpa batas tanggal**. `statusBreakdown` hanya memuat
status yang jumlahnya > 0.

##### `POST /api/v1/students` — administrator + CSRF

Request `application/json` (maks 1 MiB):

```json
{ "name": "Fajar Nugroho", "nim": "241011006", "classId": "IK24-A" }
```

`nim` wajib **9 digit angka** (mis. `241011001`); `classId` di-uppercase. Respons `201`:

```json
{ "id": "student-241011006", "nim": "241011006", "name": "Fajar Nugroho", "classId": "IK24-A", "faceEnrolled": false }
```

Error: `400 invalid_json | missing_fields | invalid_nim | invalid_class`, `409 student_exists`.

##### `DELETE /api/v1/students/{nim}` — administrator + CSRF

Menghapus mahasiswa **beserta** template wajah, foto enrollment & foto scan di
Garage, seluruh event scan, dan relasi `class_students`. Tidak bisa dibatalkan.
Respons `200`: `{"nim":"241011001","name":"Alya Putri","deleted":true}`.
Error: `403 forbidden`, `404 student_not_found`.

Mahasiswa tidak di-seed, jadi mahasiswa yang dihapus tidak muncul lagi
setelah restart.

##### `GET /api/v1/staff` · `POST /api/v1/staff` · `DELETE /api/v1/staff/{id}` — administrator + CSRF

- `GET` → `{"staff":[{"id","username","displayName","role","active","classIds","createdAt"}]}` (tanpa hash password).
- `POST` membuat **akun dosen**: JSON `{"username":"rina.wibowo","displayName":"Dr. Rina Wibowo","password":"…","classIds":["IK24-A"]}`.
  Username disimpan huruf kecil (`^[a-z0-9._-]{3,32}$`), password ≥ 12
  karakter, minimal satu kelas yang ada.
  Respons `201`. Error: `400 invalid_input`, `409 username_taken`.
- `DELETE` menghapus **akun dosen** saja (`403 not_lecturer` untuk administrator,
  `404 user_not_found`). Sesi dosen itu langsung berakhir karena
  `RequireSession` memuat ulang user dari database setiap request.

##### `POST /api/v1/students/{nim}/face-template` — administrator + CSRF

`multipart/form-data` dengan **1–6** file pada field `images` (field `image`
tetap diterima; ≤ 8 MiB per foto). Pose yang disarankan: depan, kiri,
kanan, atas, bawah, senyum. Go mengirim satu request `POST /register` per
foto secara paralel (face service membatasi dua inferensi bersamaan), lalu
memastikan tiap foto berisi wajah, semua foto adalah orang yang sama, dan
wajahnya belum terdaftar atas NIM lain. Go menyimpan semua embedding 512-d ke
satu dokumen `face_templates` sesuai urutan unggahan (enrollment ulang
mengganti semua pose lama).

Enrollment bersifat all-or-nothing: bila satu request foto gagal, template
MongoDB tidak diperbarui. Face service hanya menghasilkan embedding; Go yang
menyimpan template. Respons fragment menampilkan toast dan pesan inline, serta
indikator selama unggahan/pemrosesan. Jika koneksi terputus atau server
mengembalikan error tanpa fragment, UI meminta operator memuat ulang roster
untuk memeriksa apakah template aktif sebelum mencoba ulang.

```bash
curl -b cookie.txt -H "X-CSRF-Token: <token>" \
  -F images=@depan.jpg -F images=@kiri.jpg -F images=@kanan.jpg \
  http://localhost:8080/api/v1/students/241011001/face-template
```

Respons `201`:

```json
{
  "nim": "241011001",
  "faceEnrolled": true,
  "result": {
    "studentId": "student-241011001", "templateCount": 3, "dimension": 512,
    "detectionConfidences": [0.93, 0.91, 0.9], "processingTimeMs": 412.7,
    "imageStored": true,
    "imageUrls": [
      "/api/v1/images/enroll/student-241011001/20261028T080000Z-3f9a1c2b4d5e.jpg",
      "/api/v1/images/enroll/student-241011001/20261028T080000Z-a1b2c3d4e5f6.jpg",
      "/api/v1/images/enroll/student-241011001/20261028T080000Z-0a1b2c3d4e5f.jpg"
    ]
  }
}
```

`imageStored`/`imageUrls` hanya muncul bila penyimpanan foto aktif. Gagal
menyimpan foto (Garage mati) **tidak** menggagalkan enrollment:
`imageStored: false`, template tetap tersimpan.

Error (pesan menyebut nomor foto bila relevan, mis. "foto ke-2"):

| Status | `error` | Arti |
| --- | --- | --- |
| 400 | `invalid_image` | tidak ada foto, lebih dari 6 foto, foto > 8 MiB, atau tidak bisa di-decode |
| 404 | `student_not_found` | NIM tidak ada |
| 422 | `no_face` | salah satu foto tidak berisi wajah |
| 422 | `inconsistent_faces` | salah satu foto bukan orang yang sama (kemiripan dengan foto pertama dan rata-rata foto lain < `FACE_POSE_THRESHOLD`, default 0.30) |
| 409 | `face_already_enrolled` | wajah sudah terdaftar atas NIM lain |
| 502 | `face_service_error` | face service mati, model belum siap, atau timeout |

##### `POST /api/v1/scans` — perangkat

`multipart/form-data`: `device_id`, `class_id` (**opsional**; kosong = kelas
otomatis), `captured_at` (RFC3339), `liveness_passed` (`true`/`false`), dan
**1–6** file `image` berurutan (frame dari jendela scan ±2 detik; ≤ 8 MiB
per frame, total body ≤ 16 MiB). Header: `Authorization: Bearer
<DEVICE_TOKEN>`, `X-Device-Timestamp` (RFC3339), `X-Device-Signature` (hex
HMAC-SHA256).

Pesan HMAC = enam baris digabung `\n`, kunci = `DEVICE_TOKEN`:

```text
<X-Device-Timestamp RFC3339>
<device_id>
<class_id huruf besar, atau kosong>
<captured_at persis seperti field>
<liveness_passed lowercase true/false>
<hex SHA-256 dari semua frame digabung berurutan>   ← satu frame = SHA-256 gambar itu
```

Urutan validasi: token → frame → `device_id` → `captured_at` (±5 menit) →
`liveness_passed` → HMAC & `X-Device-Timestamp` (±2 menit) → perangkat
terdaftar → kelas ada (bila `class_id` diisi) → tiap frame ke face service
`POST /embed` → pencocokan cosine similarity di Go terhadap semua pose
`face_templates` (ambang `FACE_SIM_THRESHOLD`, default 0.45). Go berhenti
memproses frame begitu satu mahasiswa cocok di `SCAN_MIN_MATCH_FRAMES`
(default 2) frame. Frame tanpa wajah atau rusak dilewati.

Penentuan status (mahasiswa dengan frame cocok terbanyak menang):

| Kondisi | `status` | `result` | `reviewReason` |
| --- | --- | --- | --- |
| tidak ada wajah di semua frame | `Ditolak` | `wajah_tidak_terdeteksi` | "Wajah tidak terdeteksi pada 4 frame." |
| ada wajah, kemiripan < ambang / belum ada template | `Ditolak` | `tidak_terdaftar` | "Wajah tidak terdaftar (kemiripan tertinggi 0.31 < 0.45)." |
| cocok, `class_id` diisi dan mahasiswa bukan anggotanya | `Perlu ditinjau` | `perlu_ditinjau` | Mahasiswa terverifikasi tetapi bukan anggota kelas pada perangkat. |
| cocok di ≥ `SCAN_MIN_MATCH_FRAMES` frame, atau `liveness_passed=true` | `Hadir` | `hadir` | — |
| cocok di kurang dari `SCAN_MIN_MATCH_FRAMES` frame | `Perlu ditinjau` | `perlu_ditinjau` | "Wajah cocok di 1 dari 4 frame; minimal 2 frame agar langsung hadir." |

Dengan `class_id` kosong, event dicatat di **kelas mahasiswa yang cocok**
(`students.class_id`); event wajah tidak dikenal disimpan dengan `class_id`
kosong (terlihat oleh administrator di halaman Log). Event disimpan ke
`scan_events` (ditambah `frames_total`, `frames_with_face`,
`frames_matched`) dan `devices.last_seen` diperbarui. Respons `202`:

```json
{
  "status": "Hadir",
  "result": "hadir",
  "message": "Hadir",
  "student": { "id": "student-241011001", "nim": "241011001", "name": "Alya Putri" },
  "classId": "IK24-A",
  "faceVerified": true,
  "confidence": 0.91,
  "reviewReason": "",
  "framesTotal": 4,
  "framesWithFace": 2,
  "framesMatched": 2
}
```

`student` bernilai `null` bila wajah tidak cocok; `message` adalah teks
singkat untuk layar perangkat (`"Wajah tidak Terdaftar"`, `"Wajah tidak
terdeteksi"`, `"Hadir"`, atau alasan peninjauan). Bila face service gagal,
respons `502` dan **tidak ada** event yang dicatat.

> Konsensus beberapa frame **bukan** anti-spoof: foto cetak di depan kamera
> bisa tercatat Hadir. Lihat `bagian Perangkat` §7.

Bila penyimpanan foto aktif, **satu** frame bukti (frame dengan skor terbaik,
atau frame berwajah pertama) diunggah ke Garage (batas 3 detik) sebelum event
disimpan, dan kuncinya dicatat di `scan_events.image_key`. Gagal unggah hanya
dicatat di log; event tetap disimpan tanpa foto.

##### `GET /api/v1/images/{key...}` — staf

Mengalirkan foto enrollment (`enroll/...`) atau scan (`scans/...`) dari
Garage lewat Go; browser tidak pernah mengakses Garage langsung. Kunci harus
tercatat di `face_templates.image_key` atau `scan_events.image_key`.
Administrator boleh melihat semua foto; dosen hanya foto dari kelasnya.
Error: `401`, `403 forbidden`, `404 image_not_found` (termasuk foto yang sudah
dihapus karena retensi), `404 image_storage_disabled`, `502 image_storage_error`.
Halaman **Log** menampilkan tautan "Lihat" pada kolom Foto.

Penting: `liveness_passed` adalah klaim perangkat yang ditandatangani, bukan
deteksi biologis. HMAC melindungi integritas pesan, bukan membuktikan bahwa
wajah di gambar adalah orang hidup. Belum ada nonce anti-replay.

Firmware perangkat yang memakai kontrak ini: `devices/esp32_scanner/` (lihat `bagian Perangkat` §2). Perangkat commissioning didokumentasikan terpisah dan tidak termasuk runtime server.

##### `GET|POST /api/v1/devices/ping` — perangkat

Heartbeat untuk status "Terhubung". ID dibaca dari form `device_id`, query
`device_id`, atau header `X-Device-Id` (default `ESP32-GED-A1`). Token
`DEVICE_TOKEN` wajib. Perangkat harus sudah terdaftar; heartbeat memperbarui
`last_seen` tanpa membuat perangkat baru.

```json
{ "status": "ok", "device_id": "ESP32-GED-A1", "last_seen": "2026-10-21T00:15:00Z", "message": "Perangkat berhasil terhubung dan status aktif." }
```

#### 5. Fragment htmx (`/partials/*`)

Semua wajib session staf, membalas HTML, dan memakai query ber-scope role.
Sesi tidak valid → `401` JSON (`app.js` lalu mengalihkan ke `/login`).
POST/DELETE wajib CSRF (`app.js` menambah header `X-CSRF-Token` otomatis).
Filter `class` dari dosen yang bukan kelasnya menghasilkan data kosong.

| Endpoint | Guna | Akses | Penyegaran |
| --- | --- | --- | --- |
| `GET /partials/dashboard` | metrik, kehadiran per kelas, status perangkat/face service, 8 baris pertama roster hari ini (beserta scan terakhirnya) | staf | `load, every 15s` |
| `GET /partials/kelas` | ringkasan + tabel kelas + form pengelolaan kelas | staf | `load, every 15s` |
| `POST /partials/kelas` | tambah kelas (`classID`, `name`); kelas dosen otomatis masuk cakupan pembuat | administrator, dosen | — |
| `DELETE /partials/kelas/{classID}` | hapus kelas yang tidak dipakai mahasiswa, perangkat, riwayat, atau akun staf lain | administrator, dosen (dosen hanya kelas dalam cakupannya) | konfirmasi `hx-confirm` |
| `GET /partials/laporan` | rekap hari ini, komposisi status, kehadiran per kelas, 50 scan terbaru | staf | `load, every 15s` |
| `GET /partials/mahasiswa?q=&class=` | tabel roster (+ ringkasan via `hx-swap-oob`) | staf | `load, every 15s`, ketik, ganti filter, event `roster-changed` |
| `GET /partials/mahasiswa/form` | form dialog pendaftaran | administrator | sekali saat halaman dimuat |
| `POST /partials/mahasiswa/students` | tambah mahasiswa (form-urlencoded `name`, `nim`, `classId`); dialog tetap terbuka sampai roster memuat mahasiswa baru | administrator | — |
| `POST /partials/mahasiswa/{nim}/face-template` | enrollment / enrollment ulang wajah (multipart `images`, 1–6 foto dari kamera browser; filter `q`/`class` diteruskan sebagai query) | administrator | — |
| `DELETE /partials/mahasiswa/{nim}` | hapus mahasiswa + data wajah, foto, riwayat scan (konfirmasi `hx-confirm`; `q`/`class` lewat `hx-include`) → roster | administrator | — |
| `GET /partials/akses/staff` | form buat dosen + tabel akun staf | administrator | sekali saat halaman `/akses` dimuat |
| `POST /partials/akses/lecturers` | buat akun dosen (form `username`, `displayName`, `password`, `classIds` berulang) → fragment akun | administrator | — |
| `DELETE /partials/akses/staff/{id}` | hapus akun dosen (konfirmasi) → fragment akun | administrator | — |
| `GET /partials/logs?q=&status=&class=` | log pemindaian seluruh waktu, maks 200 baris (+ ringkasan via `hx-swap-oob`) | staf | `load, every 15s`, ketik, ganti filter |

ID kelas dinormalisasi menjadi huruf besar dan harus 2–32 karakter alfanumerik
atau tanda hubung; nama wajib diisi dan maksimal 120 karakter. Kelas awal yang
dihapus ditandai agar tidak dibuat ulang saat server restart. Penghapusan gagal
jika kelas masih direferensikan mahasiswa, relasi `class_students`, perangkat,
riwayat scan, atau akun staf lain. Opsi kelas pada halaman Mahasiswa dan Log
dibaca dari database dan dibatasi ke kelas dosen yang sedang login.

Konvensi:

- **Sukses tambah mahasiswa** → header `HX-Trigger-After-Swap` mengirim
  `student-created`; browser mengosongkan pencarian, mengatur filter ke Semua
  kelas, memuat ulang seluruh roster, lalu menutup dialog setelah swap selesai.
  Ini memastikan mahasiswa baru dan semua mahasiswa lain tampil sebelum
  enrollment.
- Akun dosen yang berhasil dibuat langsung muncul bersama semua akun lain;
  urutan daftar menampilkan akun terbaru lebih dahulu dalam role masing-masing.
- **Sukses POST lainnya** → HTML + header `HX-Trigger` JSON untuk toast/notifikasi.
  Enrollment, hapus mahasiswa, dan aksi akun dosen mengembalikan fragmen yang
  sama (roster / akun) dengan pesan sukses atau gagal di atas tabel.
- Enrollment wajah dimulai dari kamera pada perangkat administrator. Browser
  meminta izin langsung ketika tombol Enroll wajah ditekan (aksi pengguna);
  browser tetap memerlukan persetujuan dan dapat mengingat izin per-origin.
  Dialog enrollment menyediakan pilihan perangkat jika lebih dari satu kamera
  tersedia, lalu mengirim 1–6 foto JPEG langsung sebagai satu request
  multipart eksplisit ke endpoint yang sama. UI menunggu respons dan
  refresh roster, serta mempertahankan foto jika request gagal agar dapat
  dicoba ulang. Jika izin tidak muncul, periksa izin kamera browser dan
  `Permissions-Policy` pada server/proxy. Kamera browser memerlukan HTTPS
  dengan sertifikat valid (localhost diizinkan untuk pengembangan).
- **Hapus** memakai `hx-confirm` (dialog konfirmasi browser) sebelum request
  `DELETE` dikirim.
- **Gagal validasi** → **HTTP 200** dengan pesan inline di form/region, bukan
  status 4xx. Non-administrator → `403` teks biasa.
- Ringkasan `/partials/logs` (total, terverifikasi, ditinjau, ditolak)
  dihitung untuk **seluruh waktu** dalam cakupan role dan **tidak** ikut
  filter; tabelnya mengikuti filter. Pencarian `q` mencocokkan nama atau NIM
  (regex case-insensitive, input di-escape).

#### 6. Role

| Kemampuan | administrator | dosen | mahasiswa |
| --- | --- | --- | --- |
| Ringkasan, kelas, laporan, log, roster | semua kelas | kelas di `users.class_ids` | tidak ada akses |
| Daftarkan & hapus mahasiswa | ya | tidak | — |
| Buat & hapus akun dosen (halaman `/akses`) | ya | tidak | — |
| Enrollment wajah | ya | tidak | — |
| Halaman `/akses` | ya | dialihkan ke `/` | — |
| Absensi | — | — | lewat scan perangkat |

Mahasiswa adalah record roster/subjek absensi tanpa akun aplikasi. Tidak ada
`STUDENT_TOKEN` atau endpoint akses mahasiswa. Dosen tanpa kelas yang
ditugaskan melihat data kosong.

#### 7. Privasi biometrik

- Template wajah (embedding) tidak pernah dikirim ke browser.
- Foto enrollment dan scan disimpan di bucket Garage privat bila `S3_*`
  diisi, dan hanya bisa dilihat staf lewat `GET /api/v1/images/...` dengan
  pembatasan kelas. Foto scan dihapus otomatis setelah
  `IMAGE_RETENTION_DAYS` (Hadir) / `IMAGE_REVIEW_RETENTION_DAYS` (lainnya);
  foto enrollment lama dihapus saat enrollment ulang.
- Menghapus mahasiswa (`DELETE /api/v1/students/{nim}` atau tombol di halaman
  Mahasiswa) menghapus template, foto enrollment, foto scan, dan riwayat
  scannya sekaligus.
- Lindungi template dengan kontrol akses ketat, enkripsi backup, retensi
  terbatas, serta prosedur persetujuan.


## Arsitektur

### Arsitektur Algorithmics Face Recognation

> Status: **arsitektur yang berlaku saat ini** (Go SSR + htmx, satu binary,
> MongoDB, foto opsional di S3 Garage). Terakhir diselaraskan dengan kode:
> 26 Oktober 2026.
> Dokumen ini menggantikan draf rencana pemisahan frontend/Nuxt yang ditolak —
> lihat §9 untuk catatannya.

#### 1. Ringkasan

Satu aplikasi Go yang merender HTML dan sekaligus melayani API. Tidak ada
runtime kedua, tidak ada build frontend terpisah, tidak ada langkah deploy
kedua.

- **Bahasa/runtime**: Go (satu binary) + MongoDB.
- **Frontend**: `html/template` server-rendered + htmx untuk navigasi/aksi
  tanpa reload penuh.
- **Layanan pendukung**: face service Python (internal, tidak diekspos) dan
  object storage S3 **Garage** untuk foto wajah (opsional, privat).
- **Klien**: browser staf (admin/dosen) dan perangkat absensi ESP32 + ESP32-CAM
  (scan wajah + heartbeat, `devices/esp32_scanner` & `devices/esp32cam_uart`).
  Firmware tambahan hanya untuk commissioning perangkat.

Konsekuensi yang disengaja: halaman dan API berbagi satu sumber query/validasi
(`internal/api`), sehingga aturan role tidak bisa berbeda antara tampilan dan
API.

#### 2. Komponen

| Lapisan | Lokasi | Peran |
| --- | --- | --- |
| Entrypoint & routing | `cmd/server/main.go` | wiring route, middleware, bootstrap, env |
| HTTP API & domain | `internal/api/server.go`, `admin.go`, `face.go` | endpoint staf + perangkat, query ber-scope role, error sentinel; fitur administrator (hapus mahasiswa, akun dosen); klien face service |
| Halaman & fragment | `internal/controller/` | render `views/pages` (shell) dan `views/fragments` (htmx) |
| Keamanan | `internal/security/middleware.go` | `RequireSession`, `RequireRole`, `Principal`, security headers/CSP |
| Penyimpanan | `internal/store/database.go`, `users.go` | koneksi, index, seed MongoDB; akun staf (bcrypt) |
| Foto wajah | `internal/api/images.go`, `internal/storage/s3.go` | unggah/stream/retensi foto di Garage (S3 SigV4, pustaka standar) |
| Tampilan | `views/` | `layouts/base.html`, `pages/*`, `fragments/*` |
| Aset statis | `static/` | `css/`, `js/app.js`, `js/lib/ui.js`, vendor lokal |
| Face Recognition Service | [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face) | Python stateless; kontrak `/health`, `/embed` (1 foto), dan `/register`; Go mengirim enrollment satu request per foto secara paralel |
| Firmware | `devices/` | `esp32_scanner` (perangkat absensi → `/api/v1/scans`, `/devices/ping`), `esp32cam_uart` (kamera via UART); firmware lain hanya untuk commissioning |

Vendor frontend di-host lokal dan versi-terpin: htmx 2.0.11, Lucide 1.59.0,
dengan `htmx.config.allowEval = false`.

#### 3. Alur data

##### Halaman staf (browser)

```text
Browser ──GET /,/mahasiswa,/kelas,/laporan,/akses──► Go (shell htmx)
        ──GET /partials/...──────────────────────► Go (fragment HTML)
        ──POST /partials/... (+ X-CSRF-Token)────► Go → internal/api → MongoDB
                                                    └── HX-Trigger: toast / roster-changed / ...
```

Navigasi sidebar memakai htmx `hx-boost` untuk mengambil halaman server-rendered
berikutnya lalu mengganti `#main-content` (`hx-target` + `hx-select`). Sidebar
dan ikon navigasi tetap di DOM, sehingga tidak ikut dimuat ulang. Aksi dan
pembaruan data memakai fragment HTML di `/partials/*` (bukan JSON + perakitan
DOM); ikon Lucide disegarkan setelah swap fragment.

##### Perangkat (ESP32 + ESP32-CAM) — scan

```text
HC-SR04 → ESP32 ──UART "ON", "CAP"×4 (±2 s), "OFF"──► ESP32-CAM (stream) ──JPEG + CRC32──► ESP32
ESP32 ──POST /api/v1/scans (1–6 frame, bearer DEVICE_TOKEN + HMAC + timestamp)──► Go (scan.go)
                                                                       ├── Python /embed per frame → embedding
                                                                       ├── Go: cosine vs semua pose; ≥ 2 frame cocok → Hadir
                                                                       ├── kelas: CLASS_ID perangkat, atau kelas mahasiswa (kosong)
                                                                       ├── Garage: scans/... (opsional, maks 3 s)
                                                                       ├── MongoDB: scan_events (+ image_key)
                                                                       └── MongoDB: devices.last_seen
```

##### Perangkat (ESP8266) — heartbeat

```text
ESP8266 ──POST /api/v1/devices/ping (bearer DEVICE_TOKEN wajib)──────────► Go
                                                                           └── MongoDB: devices.last_seen (perangkat terdaftar)
```

Dashboard menganggap perangkat online bila `last_seen` < 2 menit.

##### Enrollment wajah (admin)

```text
Admin ──kamera browser (HTTPS) → 1–6 foto pose ──POST /partials/mahasiswa/{nim}/face-template──► Go
                                                                   ├── Python /register → embedding per foto
                                                                   ├── Go: cek tiap foto ada wajah, satu orang, belum terdaftar di NIM lain
                                                                   ├── MongoDB: face_templates.encodings (1–6 vektor)
                                                                   └── Garage: enroll/... per foto (opsional; foto lama dihapus)
```

##### Administrasi (administrator)

```text
Admin ──DELETE /partials/mahasiswa/{nim} (hx-confirm)──► Go ──► Garage: hapus foto enroll/ & scans/ mahasiswa itu
                                                         └──► MongoDB: hapus scan_events, face_templates, class_students, students
Admin ──POST /partials/akses/lecturers──► Go: validasi username/password/kelas ──► MongoDB: users (role dosen, bcrypt)
Admin ──DELETE /partials/akses/staff/{id}──► Go: hanya role dosen ──► MongoDB: hapus users (sesi dosen langsung tidak berlaku)
```

##### Foto wajah (staf)

```text
Staf ──GET /api/v1/images/{key} (session)──► Go: cek key di MongoDB + kelas dosen ──► Garage (GET) ──► stream ke browser
Go (tiap jam) ──► hapus foto scan lewat masa retensi di Garage + $unset image_key
```

Browser tidak pernah mengakses Garage langsung; bucket tetap privat.

##### API staf (integrasi)

`/api/v1/*` tetap tersedia sebagai API JSON untuk integrasi dan
perangkat. Sejak migrasi htmx, halaman **tidak** lagi membaca
`/api/v1`; sumber data halaman adalah `/partials/*`. Detail payload dan skema
HMAC ada di `bagian Kontrak API`.

#### 4. Keputusan arsitektur (ADR)

##### ADR-1 — Frontend tetap Go SSR + htmx (Diterima)

- **Konteks**: sempat dipertimbangkan frontend terpisah (Nuxt) yang mengonsumsi
  Go sebagai API murni, dengan kekhawatiran Go + htmx "berat" dan ikon berkedip
  saat navigasi/refresh.
- **Keputusan**: pertahankan Go SSR + htmx sebagai satu aplikasi.
- **Dasar**: navigasi htmx mengganti `#main-content` tanpa memuat ulang
  sidebar/ikon; pembaruan data memakai fragment HTML. Interaksi terasa ringan
  dengan satu runtime, satu deploy, CSP ketat tetap utuh, dan rantai pasok
  tetap ramping (tanpa `node_modules`). Lihat §9 untuk alasan lengkap penolakan
  Nuxt.
- **Konsekuensi**: jika suatu saat muncul kebutuhan klien frontend lain (mis.
  aplikasi mobile resmi), `/api/v1` sudah menjadi jalur JSON yang stabil —
  pemisahan bisa dilakukan saat itu, dengan alasan konkret, bukan sekarang.

##### ADR-2 — Inference wajah di Python, penyimpanan & pencocokan di Go (Diterima, direvisi 28 Okt 2026)

- **Keputusan**: model deteksi & embedding wajah dijalankan oleh Face
  Recognition Service Python (source dan deployment di
  [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face)).
  Go memvalidasi perangkat/role, memanggil `POST /embed`
  (scan) dan `POST /register` (satu request per pose secara paralel saat
  enrollment), menyimpan embedding, dan mencocokkannya. Revisi 28 Okt:
  `/detect` dihapus dari Python; satu mahasiswa kini punya beberapa embedding
  pose dan skornya adalah pose terbaik.
  Enrollment Go mengirim satu request per foto secara paralel; layanan Python
  menjalankan paling banyak dua inferensi pada worker thread sekaligus.
- **Konsekuensi**: service Python tidak pernah diekspos ke jaringan publik;
  Go mengirim `FACE_SERVICE_TOKEN` sebagai Bearer. Implementasi validasi token
  dan kebijakan endpoint berada di repository Face Recognition Service.

Revisi: Face Recognition Service bersifat stateless (deteksi + embedding
saja); source service dipelihara di repository terpisah. Go menyimpan
embedding di `face_templates` dan melakukan pencocokan 1:N dengan cosine
similarity (`internal/api/face.go`). Dengan begitu hanya Go yang mengakses
MongoDB dan hanya Go yang menjadi API publik untuk dashboard maupun ESP32.

##### ADR-7 — Foto wajah di object storage S3 (Garage), bukan MongoDB (Diterima, 26 Okt 2026)

- **Konteks**: pencocokan hanya butuh embedding, tetapi foto diperlukan untuk
  enrollment ulang saat model diganti, peninjauan scan, dan audit.
- **Keputusan**: simpan foto di bucket Garage privat; MongoDB hanya menyimpan
  kuncinya (`image_key`). Klien S3 ditulis dengan pustaka standar (SigV4)
  agar tidak menambah dependensi.
- **Konsekuensi**: penyimpanan foto bersifat *best effort* — Garage mati tidak
  menghentikan absensi. Foto scan punya retensi (30/90 hari); foto
  enrollment disimpan selama template masih ada.

##### ADR-3 — htmx memakai fragment server-rendered, bukan JSON + DOM (Diterima)

- **Keputusan**: halaman terautentikasi mengambil HTML dari `/partials/*`
  (butuh session); `/api/v1/*` dipertahankan sebagai API integrasi, bukan
  sumber data halaman.
- **Konsekuensi**: validasi dan otorisasi tetap di server; tidak ada logika
  role di klien. Error form dikembalikan inline (HTTP 200) agar muncul di
  dalam form, sedangkan sukses memicu event via `HX-Trigger`.

##### ADR-4 — Dua mekanisme auth yang terpisah (Diterima)

- **Staf**: session cookie `itbjh_session` (HttpOnly, SameSite=Lax, 8 jam, idle
  30 menit) + CSRF `itbjh_csrf` (gorilla). `RemoteAddr` untuk rate limit login
  (10 / 5 menit).
- **Perangkat**: bearer `DEVICE_TOKEN` + HMAC-SHA256 + timestamp.
- **Konsekuensi**: perangkat tidak punya jalur ke endpoint staf, dan sebaliknya.

##### ADR-5 — CSP ketat, aset di-vendor lokal (Diterima)

- **Keputusan**: `script-src 'self'`, `allowEval = false`, tanpa CDN; JS/font
  pihak ketiga di-host sendiri dan versi-terpin.
- **Konsekuensi**: tidak ada permukaan skrip eksternal; kebijakan bisa
  dikencangkan tanpa menyesuaikan kode halaman.

##### ADR-6 — MongoDB sebagai basis data (Diterima)

- **Konteks**: versi awal memakai SQLite berkas lokal (`data/attendance.db`)
  yang dibagi Go dan Python.
- **Keputusan**: pindah ke MongoDB di server terpisah. `MONGODB_URI` wajib
  dikonfigurasi tanpa URI default; nama database default `attendance`.
- **Dasar** (disimpulkan dari kode — koreksi bila alasan sebenarnya berbeda):
  Go dan face service Python dapat berjalan di mesin berbeda dan mengakses
  data yang sama lewat jaringan, tanpa berbagi berkas SQLite.
- **Konsekuensi**: penugasan kelas dosen pindah ke `users.class_ids`; waktu
  disimpan sebagai string RFC3339 UTC dan "hari ini" dihitung di Go
  (`todayRangeUTC`); MongoDB wajib di-hardening (auth, bindIp, TLS, backup).
  Detail di `bagian Database`.

#### 5. Batas kepercayaan

1. **Internet → Go (browser)**: session + CSRF; role & kelas diambil dari DB
   tiap request, bukan dari klien.
2. **Internet → Go (perangkat)**: scan memakai device token + HMAC +
   timestamp. Heartbeat ping wajib memakai token;
   `GET /api/v1/health` publik.
3. **Go → Face service**: jaringan internal, bearer `FACE_SERVICE_TOKEN`;
   port Python tidak diekspos.
4. **Go → MongoDB**: jaringan internal; di production wajib user aplikasi +
   `authorization: enabled` (panduan di `panduan utama`). Face service Python
   tidak mengakses MongoDB.
5. **Go → Garage (S3)**: jaringan internal, kunci akses khusus bucket
   (bukan `admin_token`). Browser tidak mengakses Garage.

#### 6. Ringkasan kontrak API

Acuan payload/skema ada di `bagian Kontrak API`.

| Kelompok | Contoh | Auth |
| --- | --- | --- |
| Staf baca | `GET /api/v1/dashboard`, `/classes`, `/reports` | session |
| Staf tulis | `POST /api/v1/students`, `POST /api/v1/students/{nim}/face-template` | session + CSRF (admin) |
| Perangkat — scan | `POST /api/v1/scans` | device token + HMAC + timestamp |
| Staf baca foto | `GET /api/v1/images/{key...}` | session (dosen: hanya kelasnya) |
| Perangkat — heartbeat | `GET\|POST /api/v1/devices/ping` | device token (wajib) |
| Publik | `GET /api/v1/health` | — |
| Fragment (staf) | `GET/POST /partials/*` | session (+ CSRF untuk POST) |

#### 7. Konfigurasi & deployment

Satu proses di belakang reverse proxy/TLS. Face service dan database berada di
jaringan internal.

Variabel lingkungan (dari `cmd/server/main.go`):

| Variabel | Keterangan |
| --- | --- |
| `HTTP_ADDR` | alamat listen (default `:8080`) |
| `MONGODB_URI` | URI koneksi MongoDB (wajib; tidak ada URI default) |
| `MONGODB_DATABASE` | nama database MongoDB (default `attendance`) |
| `STATIC_DIR`, `VIEW_DIR` | lokasi aset & template |
| `APP_ENV` | wajib `production`; nilai lain ditolak |
| `CSRF_SECRET` | wajib ≥32 karakter |
| `DEVICE_TOKEN` | wajib ≥32 karakter |
| `FACE_SERVICE_URL`, `FACE_SERVICE_TOKEN` | koneksi privat ke Python (token wajib ≥32 karakter dan diverifikasi Python) |
| `S3_ENDPOINT`, `S3_REGION`, `S3_BUCKET`, `S3_ACCESS_KEY_ID`, `S3_SECRET_ACCESS_KEY` | penyimpanan foto di Garage (kosong semua = nonaktif) |
| `IMAGE_RETENTION_DAYS`, `IMAGE_REVIEW_RETENTION_DAYS` | retensi foto scan (default 30 / 90 hari) |
| `BOOTSTRAP_ADMIN_*`, `BOOTSTRAP_LECTURER_*` | pembuatan akun awal |

`FACE_SIM_THRESHOLD` (default `0.45`, ambang cosine similarity) dan
`FACE_SERVICE_TIMEOUT_SECONDS` (default `15`) dibaca oleh Go; untuk enrollment nilainya dikali jumlah
gelombang inferensi (dua request aktif bersamaan). `FACE_SERVICE_URL`
default `http://127.0.0.1:8000`. Templat lengkap: `.env.example`. Zona waktu
server menentukan batas "hari ini" pada ringkasan dan laporan.

`.env` dimuat dengan `godotenv.Overload`, jadi nilainya menimpa environment
proses (lihat `bagian Panduan Kode` §8). Bila `.env` tidak ada, konfigurasi wajib
disediakan lewat environment proses. Password staf minimal 12 karakter.

Catatan hardening: HSTS **belum** dipasang (lihat §10). Header yang sudah ada:
`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy:
same-origin`, dan CSP.

#### 8. Invarian

- Otorisasi hanya di server Go, dari DB — bukan dari role/query/header klien.
- Template wajah (embedding) tidak pernah dikirim ke browser; foto hanya
  lewat `GET /api/v1/images/...` setelah cek role/kelas.
- Kegagalan penyimpanan foto tidak boleh menggagalkan scan atau enrollment.
- Endpoint perangkat terpisah dari endpoint staf.
- CSP tidak boleh memasukkan `unsafe-inline` untuk `script-src`.
- Tidak ada akun login mahasiswa tanpa keputusan produk baru.

#### 9. Opsi yang ditolak (arsip)

**Pemisahan frontend: Go API + Nuxt di domain berbeda.** Ditolak karena:

1. Menambah runtime kedua (Node) dan langkah deploy kedua, padahal satu Go
   binary sudah cukup; kekhawatiran "htmx berat" dan ikon berkedip tidak
   terbukti.
2. Harus merombak auth: dashboard dan API akan cross-site, sehingga cookie
   session + CSRF tidak bisa dipakai apa adanya. Perlu BFF atau token + CORS.
3. CSP harus dilonggarkan (nonce/hash) untuk bundle dan hidrasi Nuxt.
4. Menambah ratusan paket npm transitif, padahal CDN baru saja dihapus dan
   dependensi JS di-vendor lokal secara terpin.
5. Tidak ada kebutuhan klien frontend kedua saat ini; ESP32 sudah dilayani API
   JSON yang sama, dan `/api/v1` tetap tersedia jika itu berubah.

Dicatat agar tidak diusulkan ulang tanpa alasan baru yang konkret.

#### 10. Risiko terbuka & TODO backend

Catatan deployment dan risiko yang masih perlu ditangani (lihat juga
[panduan operasional](#operasional-handoff)):

1. **Session & rate limit in-memory** → belum aman untuk multi-instance; butuh
   store bersama (Redis/DB) bila di-scale.
2. **Satu `DEVICE_TOKEN` global** untuk semua perangkat → butuh secret per
   perangkat + rotasi/revokasi.
3. **Belum ada anti-replay nonce** pada `POST /api/v1/scans`.
4. **Liveness** masih klaim perangkat (`liveness_passed`), bukan deteksi
   biologis tervalidasi.
5. **Ambang cosine `0.45` belum dikalibrasi**; pencocokan di Go memindai
   semua template tiap scan (belum ada pembatasan kandidat per kelas/cache).
6. **Audit & privasi**: belum ada alur persetujuan (consent), audit enrollment
   / aksi administrator, atau enkripsi backup. Retensi foto scan otomatis dan
   hapus mahasiswa ikut menghapus data biometriknya.
7. **Hardening HTTP**: HSTS dan audit logging belum ada.
8. **Kredensial lama di firmware ESP8266** (Wi-Fi + `DEVICE_TOKEN`) pernah
   masuk riwayat Git; rotasi token dan password Wi-Fi yang terdampak, dan
   jangan gunakan sketch tersebut sebagai firmware produksi.
9. **Face service kini mewajibkan `FACE_SERVICE_TOKEN`** di semua endpoint;
   pertahankan port Python pada loopback/jaringan privat dan batasi firewall.
10. **Konfigurasi hanya production**: startup menolak `APP_ENV` lain,
    secret yang kurang dari 32 karakter, dan `MONGODB_URI` kosong.
11. **Commissioning kamera Wi-Fi dan kamera stream** memakai HTTP tanpa TLS
    atau autentikasi penuh; gunakan hanya pada jaringan commissioning
    terisolasi, bukan jaringan produksi.
12. **Sisa mahasiswa contoh dari seed lama** (`IK24001`–`IK24005`) tidak
    dihapus otomatis; seed kini hanya kelas dan perangkat (lihat
    [panduan database](#database)).

#### 11. Berkas acuan

- `panduan utama` — setup & konfigurasi.
- `bagian Kontrak API` — payload API & skema HMAC perangkat.
- `panduan operasional` — status implementasi, keputusan UI, daftar risiko.
- `cmd/server/main.go` — wiring route, middleware, env, bootstrap.
- `internal/api/server.go` — endpoint + query ber-scope role.
- `internal/api/admin.go`, `internal/controller/admin_partials.go` — fitur administrator.
- `internal/security/middleware.go` — session, CSRF, principal, CSP.
- `internal/controller/` — handler halaman & fragment.
- `internal/store/database.go` — koneksi, index, seed MongoDB.
- `bagian Database` — skema koleksi & operasi.
- `bagian Perangkat` — firmware & protokol perangkat.
- `bagian Integrasi Face Service` — integrasi face service dan penyimpanan foto (§8).
- `internal/api/images.go`, `internal/storage/s3.go` — foto wajah di Garage.
- `diagram.excalidraw` — diagram tahapan proyek & alur sistem.


## Panduan Kode

### Panduan Kode Algorithmics Face Recognation

Dokumen ini menjelaskan **bagaimana kode disusun dan mengapa**, agar perubahan
berikutnya konsisten dengan pola yang ada. Untuk gambaran besar, lihat
[arsitektur](#arsitektur). Untuk kontrak API, lihat [kontrak API](#kontrak-api).
Untuk skema data, lihat [panduan database](#database). Terakhir diselaraskan dengan
kode: 26 Oktober 2026 (malam).

#### 1. Prinsip yang dipegang

1. **Server-rendered, bukan SPA.** Halaman dirender Go dengan `html/template`;
   htmx hanya menukar potongan HTML. Tidak ada logika role di klien.
2. **Satu sumber query & validasi.** Endpoint JSON (`/api/v1/*`) dan delapan
   endpoint fragment (`/partials/*`) memakai fungsi yang sama di
   `internal/api/server.go`. Tidak ada query MongoDB atau aturan validasi yang
   ditulis dua kali.
3. **Otorisasi di server, dari database.** Role dan daftar kelas dosen diambil
   ulang dari DB setiap request (`security.RequireSession`). Query/header role
   dari klien tidak pernah dipercaya.
4. **Tidak ada template biometrik ke browser.** Embedding tidak pernah dikirim;
   foto hanya dialirkan lewat endpoint ber-otorisasi (`/api/v1/images/...`).

#### 2. Peta direktori

```text
cmd/server/main.go            entry point: env, routing, middleware, bootstrap, shutdown
internal/api/server.go        endpoint JSON + query ber-scope role + domain method + error sentinel
internal/api/face.go          klien face service, enrollment embedding, pencocokan cosine 1:N
internal/api/scan.go          POST /api/v1/scans: frame multi, HMAC, konsensus frame, status + result
internal/api/face_models.go   proxy API manajemen model face service (khusus administrator)
internal/api/images.go        foto wajah di S3: unggah, otorisasi & stream, retensi
internal/api/admin.go         hapus mahasiswa (+ data wajah/foto/scan), buat & hapus akun dosen + endpoint JSON-nya
internal/storage/s3.go        klien S3 minimal (SigV4, gaya path) untuk Garage
internal/controller/
  web.go                      render halaman penuh (shell) + login page
  partials.go                 render fragment htmx (/partials/*)
  admin_partials.go           fragment administrator: hapus mahasiswa, akun dosen (/akses)
  auth.go                     login, logout, safeNext
internal/model/page.go        view model untuk halaman (judul, role, tanggal, csrf)
internal/security/middleware.go
                              session SCS, Principal, RequireSession, RequireRole,
                              LoginRateLimit, SecurityHeaders (CSP), Recover
internal/store/
  database.go                 Open (connect+ping MongoDB), migrate (index), seed (kelas/perangkat)
  users.go                    User, CreateUser, EnsureUser, AuthenticateUser, UserByID,
                              ListStaff, UsernameTaken, UserRole, DeleteUser, MinPasswordLength
devices/esp32_scanner/        firmware ESP32 utama (sensor, TFT, buzzer, Wi-Fi, scan, heartbeat)
devices/esp32cam_uart/        firmware ESP32-CAM (foto via UART)
devices/                        firmware absensi dan alat commissioning perangkat
views/
  layouts/base.html           kerangka halaman (head, sidebar, topbar, content)
  pages/*.html                shell per halaman (isi div tempat htmx menaruh fragment)
  partials/*.html             sidebar, topbar, toast
  fragments/*.html            fragment htmx ({{define}} saja)
  login.html                  dokumen login mandiri
static/css/*                  tokens, base, layout, components, pages, responsive, print
static/js/app.js              setup htmx + perilaku global
static/js/lib/ui.js           refreshIcons, toast (helper lain sisa versi pra-htmx, tidak dipakai)
static/js/vendor/*            htmx, lucide (versi terpin, di-vendor lokal)
```

#### 3. Empat alur request

##### 3.1 Halaman staf (muat penuh)

```text
GET /mahasiswa
  → sessions.LoadAndSave          (memuat sesi dari cookie itbjh_session)
  → csrf.Protect                  (menyediakan token untuk form)
  → RequireSession                (ambil user dari DB → Principal ke context)
  → Web.Students                  (render shell dari views/pages/mahasiswa.html)
```

Shell hanya mengirim kerangka + elemen dengan `hx-get`/`hx-trigger` yang
menarik fragment. Jadi HTML awal kecil, data diisi menyusul.

##### 3.2 Fragment htmx (pembaruan sebagian)

```text
GET /partials/mahasiswa
  → RequireSession (apiRequest=true)   (401 JSON bila sesi habis)
  → Partials.mahasiswa
      → api.Server.Roster / ClassRows / ...   (query ber-scope role)
  → render views/fragments/mahasiswa.html
```

Untuk POST, htmx menambahkan header CSRF lewat `htmx:configRequest` di
`static/js/app.js`:

```text
POST /partials/mahasiswa/students
  Header: X-CSRF-Token: <token dari <meta name="csrf-token">>
  → csrf.Protect memvalidasi
  → Partials.createStudent → api.Server.CreateStudent
```

Sukses **tidak** mengembalikan JSON, tapi HTML form + header
`HX-Trigger-After-Swap` dengan event `student-created`. `static/js/app.js`
mengosongkan pencarian, mengatur filter kelas ke **Semua kelas**, memuat ulang
roster secara eksplisit, dan baru menutup dialog setelah fragment roster selesai
di-swap. Request roster bersaing dibatalkan/ditahan selama langkah ini, jadi
pengguna tidak bisa mencoba enrollment sebelum baris mahasiswa tersedia.
Gagal validasi mengembalikan **HTTP 200** dengan pesan di dalam form (error
inline), bukan status 4xx.

##### 3.3 Scan perangkat ESP32

```text
POST /api/v1/scans                               (internal/api/scan.go)
  → (tanpa sesi, tanpa CSRF)
  → validDeviceToken        (Authorization: Bearer <DEVICE_TOKEN>, perbandingan konstan-waktu)
  → readScanFrames          (1–6 file "image" berurutan, maks 8 MB per frame, 16 MB total)
  → cek device_id, captured_at RFC3339 ±5 menit, liveness_passed true/false
  → validScanSignature      (HMAC-SHA256 atas X-Device-Timestamp ±2 menit + fields + framesHash)
  → cek devices._id ada; classes._id ada bila class_id diisi (kosong = kelas otomatis)
  → loadTemplates sekali, lalu matchFrames: POST /embed per frame (timeout 15 s per frame)
      → 422 "No face" / 400 → frame dilewati; error lain → 502
      → bestTemplateMatch per frame (pose terbaik ≥ FACE_SIM_THRESHOLD → 1 suara)
      → berhenti begitu satu mahasiswa punya ScanMinMatchFrames (2) suara
  → decideFrames  → mahasiswa dengan suara terbanyak, BestFrame (frame bukti)
  → kelas event: class_id perangkat, atau students.class_id bila kosong
  → scanStatus    → Hadir / Perlu ditinjau / Ditolak + result code
  → (bila S3_* diisi) unggah frame bukti ke Garage, maks 3 s → image_key
  → InsertOne scan_events (+ frames_total/with_face/matched) + UpdateOne devices.last_seen
  → log "scan <device>: <status> (<result>) frame=… wajah=… cocok=… skor=…"
  → 202 JSON {status, result, message, student, classId, faceVerified, confidence,
              reviewReason, framesTotal, framesWithFace, framesMatched}
```

Urutan penentuan status (`scanStatus`) penting dan berlapis:

| Kondisi | Status | `result` |
| --- | --- | --- |
| Tidak ada wajah di semua frame | `Ditolak` | `wajah_tidak_terdeteksi` |
| Ada wajah, tidak cocok dengan siapa pun | `Ditolak` | `tidak_terdaftar` (layar: "Wajah tidak Terdaftar") |
| Cocok, `class_id` diisi, tapi bukan anggota kelas itu | `Perlu ditinjau` | `perlu_ditinjau` |
| Cocok di ≥ `ScanMinMatchFrames` frame, atau `liveness_passed=true` | `Hadir` | `hadir` |
| Cocok di kurang dari `ScanMinMatchFrames` frame | `Perlu ditinjau` | `perlu_ditinjau` |

"Anggota kelas" dicek dengan `students.class_id == class_id` (bukan
`class_students`); dengan `class_id` kosong pemeriksaan ini tidak berlaku
karena event memakai kelas mahasiswa itu sendiri. Bila face service gagal,
respons `502` dan event **tidak** disimpan. Bila unggah foto gagal, event
**tetap** disimpan tanpa `image_key`. Konsensus frame **bukan** anti-spoof.

##### 3.4 Heartbeat perangkat

```text
GET|POST /api/v1/devices/ping
  → (tanpa sesi, tanpa CSRF)
  → device_id dari form → query → header X-Device-Id → default "ESP32-GED-A1"
  → validDeviceToken wajib
  → UpdateOne devices {$set: last_seen} (tanpa upsert; ID harus terdaftar)
  → 200 JSON {status, device_id, last_seen, message}
```

`GET /api/v1/health` hanya membaca status. Scan yang lolos validasi dan
heartbeat terautentikasi memperbarui `devices.last_seen`.

#### 4. Penjelasan per berkas

##### `cmd/server/main.go`

- Memuat `.env` dengan `godotenv.Overload` (menimpa environment; lihat jebakan
  no. 8), lalu membaca env lewat `api.ParseConfig()` dan `env()`.
- **Hanya menerima `APP_ENV=production`**; mode lain ditolak.
- **Menolak start** bila `CSRF_SECRET`, `DEVICE_TOKEN`, atau
  `FACE_SERVICE_TOKEN` < 32 karakter, `MONGODB_URI` kosong, atau MongoDB tidak
  dapat dijangkau.
- `bootstrapStaff` membuat administrator pertama dari `BOOTSTRAP_ADMIN_*` bila
  koleksi `users` belum punya administrator aktif; dosen opsional dari
  `BOOTSTRAP_LECTURER_*` (dibuat lewat `EnsureUser`, jadi dilewati bila
  username sudah ada — kelasnya tidak diperbarui). Tidak ada akun demo bawaan.
- Bootstrap hanya membuat akun awal; startup tidak pernah mengubah password,
  role, kelas, atau status akun yang sudah ada.
- `store.Open(MONGODB_URI, MONGODB_DATABASE)` — gagal konek/ping dalam 8 detik
  menghentikan proses.
- `setupImageStorage` mengaktifkan penyimpanan foto bila `S3_*` diisi:
  konfigurasi tidak lengkap menghentikan proses, bucket yang belum terjangkau
  hanya memberi peringatan. Job retensi foto berjalan tiap jam.
- Dua cabang routing:
  - `protected` (di balik session + CSRF): halaman (`/`, `/mahasiswa`,
    `/kelas`, `/laporan`, `/logs`, `/akses`), login/logout, `/api/v1/` staf,
    `/partials/`, `/static/`.
  - `router` (tanpa session): `POST /api/v1/scans`, `GET /api/v1/health`,
    `GET|POST /api/v1/devices/ping`; sisanya diteruskan ke `protected`.
- Middleware: `Recover` → `SecurityHeaders` membungkus seluruh router; cabang
  `protected` dibungkus `sessions.LoadAndSave` → `csrf.Protect`.
- Timeout server: read header 5 s, read 15 s, write 15 s, idle 60 s.
- Graceful shutdown 8 detik pada SIGINT/SIGTERM.

##### `internal/api/server.go`

Dua peran dalam satu berkas:

**a. Handler HTTP** — `StaffHandler()` (`/api/v1/dashboard|classes|reports|students…|staff…|images/…`),
`DeviceHandler()` (`/api/v1/health|scans|devices/ping`), `StaticHandler()`.
Header keamanan dipasang sekali oleh `security.SecurityHeaders` yang
membungkus seluruh router di `main.go`.

**b. Query & domain** — fungsi ber-scope role yang menerima `principal{role, classes}`:

| Fungsi | Guna |
| --- | --- |
| `classOverview` | ringkasan per kelas (jumlah mahasiswa, enrolled, hadir hari ini) |
| `dashboardAttendance` | roster + scan terakhir hari ini per mahasiswa |
| `scanStatusCounts` | hitung status scan hari ini |
| `recentEvents` | 50 event scan terbaru (tanpa batas tanggal) |
| `AllScanEvents` | 200 event terbaru dengan filter status/kelas/pencarian (halaman log) |
| `AllScanStatusCounts` | hitung status scan seluruh waktu |
| `countStudents`, `countTemplates`, `countScans` | agregat dashboard |
| `lastDeviceSeen` | dasar status online perangkat |
| `faceHealth` | cek `/health` layanan Python dengan bearer `FACE_SERVICE_TOKEN` |
| `imageStorageHealth` | cek `HEAD` bucket S3 (di `images.go`) |

Pola berulang untuk scoping dosen: bila `role == "dosen"`, filter diberi
`bson.M{"class_id": bson.M{"$in": classes}}` (atau `_id` untuk koleksi
`classes`); bila daftar kelas kosong, langsung kembalikan hasil kosong (tanpa
menyentuh DB). Data "hari ini" difilter dengan rentang string dari
`todayRangeUTC()`. Filter dari input pengguna (mis. `class` di halaman log)
diperiksa terhadap daftar kelas dosen, bukan menimpa filter role.

**c. API publik untuk controller.** Fungsi ber-scope diekspor sebagai pembungkus
bertipe supaya `internal/controller/partials.go` bisa memakainya:

```go
s.ClassRows(ctx, role, classes)      // → classOverview
s.StudentCount(ctx, role, classes)   // → countStudents
s.TemplateCount(ctx, role, classes)  // → countTemplates
s.ScanStatusCounts(...) / s.Roster(...) / s.RecentEvents(...)
s.AllScanEvents(ctx, role, classes, status, class, q) / s.AllScanStatusCounts(...)
s.DeviceStatus(ctx) / s.FaceStatus(ctx)
s.CreateStudent(ctx, name, nim, classID)
s.EnrollFace(ctx, nim, []api.EnrollImage{…})   // 1–6 foto
s.DeleteStudent(ctx, nim)                 // admin.go
s.StaffAccounts(ctx) / s.CreateLecturer(ctx, LecturerInput{…}) / s.DeleteLecturer(ctx, id)
api.ReadEnrollImages(w, r) / api.EnrollErrorMessage(err, detail) / api.LecturerErrorMessage(err)
```

**d. Error sentinel** — supaya controller bisa memetakan pesan tanpa tahu detail query MongoDB:

```go
ErrMissingFields, ErrStudentExists, ErrInvalidClass, ErrStudentNotFound, ErrFaceService
ErrInvalidImage, ErrNoFace, ErrInconsistentFaces, ErrFaceAlreadyEnrolled, ErrTooManyPhotos   // face.go (per foto: *PhotoError)
ErrImageNotFound, ErrImageForbidden                                    // images.go
ErrInvalidUsername, ErrPasswordTooShort, ErrUsernameTaken, ErrNoClasses,
ErrUserNotFound, ErrNotLecturer                                        // admin.go
```

**e. Tipe bertipe** — `ClassRow`, `ScanEvent`, `RosterEntry`. Fungsi
`classRowsToMaps` / `scanEventsToMaps` / `rosterToMaps` menjaga **bentuk JSON
lama** tetap sama meski internalnya bertipe.

##### `internal/controller/partials.go`

- `NewPartials` mem-`Glob` `views/fragments/*.html`, memasang FuncMap
  (`initials`, `statusClass`, `confidence`), lalu parse sekali saat startup.
- Setiap handler: ambil `Principal` dari context → panggil query `api` →
  rakit struct data → `p.render(w, "<nama>_fragment", data)`.
- `mahasiswaRoster` adalah helper bersama untuk `/partials/mahasiswa` dan
  `enrollFace` (keduanya mengembalikan tabel roster yang sama). Kolom
  enrollment menampilkan jumlah pose (`FaceTemplates`). Baris yang punya
  `FaceImageURLs` (dari `face_templates.image_keys`/`image_key`) menampilkan
  tombol **Lihat foto** (foto pertama) dan link nomor 2–6 yang membuka
  `/api/v1/images/enroll/...` di tab baru; izin tetap diperiksa
  `authorizeImage`. Tombol **Enroll wajah / Enroll ulang** adalah
  `<input type="file" name="images" multiple>` yang langsung dikirim saat
  file dipilih (fungsi template `inc` dipakai untuk nomor foto).
- `createStudent`, `mahasiswaForm`, dan `enrollFace` menolak non-administrator
  dengan 403.
- `admin_partials.go`: `deleteStudent` (roster setelah hapus), `staff`,
  `createLecturer`, `deleteStaff` (fragment `staff_fragment` di
  `views/fragments/akses.html`). Semua lewat `requireAdminPartial` (403 untuk
  non-administrator); gagal validasi tetap HTTP 200 dengan pesan di region.
- `logs` memanggil `AllScanStatusCounts` (ringkasan seluruh waktu, tidak ikut
  filter) dan `AllScanEvents` (tabel, ikut filter `q`/`status`/`class`).

##### `internal/controller/web.go`

- `NewWeb` mem-parse tiap halaman (`dashboard`, `mahasiswa`, `kelas`,
  `laporan`, `logs`, `akses`) bersama layout + partials (sidebar, topbar,
  toast) sekali saat startup. **Konsekuensi: mengubah `.html` memerlukan
  restart server.**
- `render` menolak bila principal tidak ada, mengalihkan non-admin dari
  `/akses`, dan mengeksekusi template `layout`.

##### `internal/security/middleware.go`

| Simbol | Perilaku |
| --- | --- |
| `NewSessionManager` | SCS; cookie `itbjh_session`, lifetime 8 jam, idle 30 menit, HttpOnly, SameSite Lax, selalu `Secure` |
| `RequireSession(…, apiRequest)` | Ambil `user_id` sesi → muat user aktif dari koleksi `users` (termasuk `class_ids`) → taruh `Principal`. Gagal: JSON 401 (`apiRequest=true`) atau redirect `/login?next=…` (`false`) |
| `RequireRole` | 403 JSON untuk `/api/`, redirect `/` untuk halaman |
| `LoginRateLimit(10, 5m)` | Per `RemoteAddr`, jendela 5 menit, `Retry-After` pada 429 |
| `SecurityHeaders` | nosniff, Referrer-Policy, X-Frame-Options DENY, Permissions-Policy, CSP, `Cache-Control: no-store` untuk non-`/static/` |
| `Recover` | Tangkap panic → 500 + log |

##### `internal/store/`

- `database.go`: `Open` (connect + ping 8 detik) → `migrate` (membuat index,
  idempoten) → `seed` (3 kelas dan 2 perangkat; idempoten via upsert
  `$setOnInsert`; tombstone `deleted_classes` mencegah kelas seed yang sudah
  dihapus muncul lagi; mahasiswa tidak di-seed). `Collection(name)` adalah
  helper akses koleksi.
- `users.go`: `CreateUser` (bcrypt, password ≥ `MinPasswordLength` = 12,
  role terbatas `administrator`/`dosen`, `_id` hex acak), `EnsureUser` (cek
  username case-insensitive), `AuthenticateUser` (username
  case-insensitive lewat regex yang di-escape; perbandingan hash dummy untuk
  menyamakan waktu saat user tidak ada), `UserByID` (menolak akun nonaktif),
  `CountUsersByRole` (hanya akun aktif), serta `ListStaff`, `UsernameTaken`,
  `UserRole`, `DeleteUser` untuk halaman `/akses`.
- `seed` berjalan di **setiap** start dengan upsert `$setOnInsert`; mahasiswa
  hanya berasal dari pendaftaran manual (lihat jebakan no. 15).

##### `internal/api/face.go`

- `postImages` mengirim multipart ke face service; respons non-2xx menjadi
  `faceServiceError{Status, Detail}` (field `detail` FastAPI).
  `classifyFaceError` memetakannya: 400 → `ErrInvalidImage`, 422 "No face" →
  `ErrNoFace` (nomor foto dari `image #N` dibungkus `*PhotoError`), lainnya →
  `ErrFaceService`.
- `embedFace` → `POST /embed` (field `image`); `registerFaces` →
  `POST /register?student_id=` satu request per foto (field `images`, file
  dinamai `foto-N.<ext>`) secara paralel.
- `enrollEmbeddings` (dipanggil `EnrollFace`): 1–`MaxEnrollPhotos` (6) foto,
  masing-masing dikirim sebagai request `/register` paralel; respons digabung
  kembali menurut urutan unggahan. `checkEnrollEmbeddings` menolak foto yang
  bukan orang yang sama (`poseConsistency` = maks kemiripan dengan foto
  pertama atau rata-rata foto lain < `Config.PoseThreshold`,
  `ErrInconsistentFaces`) dan wajah yang mirip
  template mahasiswa lain
  (`ErrFaceAlreadyEnrolled`), lalu upsert `encodings` ke `face_templates`
  (dan `$unset` field lama `encoding`).
- `bestTemplateMatch` (dipakai enrollment dan `scan.go`): `faceTemplate.vectors()`
  membaca `encodings` atau `encoding` lama, dan skor mahasiswa = pose terbaik.
- Vektor dengan dimensi berbeda diabaikan. Ambang: `Config.SimThreshold`
  (`FACE_SIM_THRESHOLD`) untuk scan dan cek duplikat; `Config.PoseThreshold`
  (`FACE_POSE_THRESHOLD`, 0.30) untuk konsistensi pose.
- Batas waktu: panggilan `/register` mendapat `FaceServiceTimeout` dikali
  jumlah gelombang inferensi (maksimal dua foto bersamaan). `ReadEnrollImages`
  memperpanjang deadline baca/tulis request menjadi `EnrollRequestTimeout`
  (3 menit) lewat `http.ResponseController`, karena server memakai
  `ReadTimeout`/`WriteTimeout` 15 detik. `storeEnrollmentImages` mengunggah
  foto ke Garage secara paralel.
- `EnrollFace` menulis log `enrollment <nim> berhasil/gagal (N foto, durasi)`.
- Pesan error untuk UI/JSON dirakit `EnrollErrorMessage` di `server.go`
  (menyebut "foto ke-N" lewat `PhotoNumber`).

##### `internal/api/images.go` dan `internal/storage/s3.go`

- `ImageStore` adalah antarmuka (Put/Get/Delete/HeadBucket); implementasinya
  `storage.S3`. `Server.images == nil` berarti penyimpanan foto nonaktif dan
  semua fungsi foto menjadi no-op.
- `uploadImage` memakai batas 3 detik dan **tidak pernah** menggagalkan
  pemanggil: gagal unggah hanya dicatat di log dan mengembalikan kunci kosong.
- `storeEnrollmentImages` mengunggah semua foto enrollment, mengisi
  `face_templates.image_keys` (semua) dan `image_key` (foto pertama), lalu
  menghapus foto enrollment lama (`enrollmentKeys` menggabungkan kedua field).
- `authorizeImage` mencari kunci `enroll/…` di `image_key` **atau** `image_keys`.
- `serveImage` (`GET /api/v1/images/{key...}`): kunci harus tercatat di
  MongoDB; `authorizeImage` membatasi dosen ke kelasnya.
- `PurgeExpiredImages` / `RunImageRetention`: hapus foto scan yang lewat
  `IMAGE_RETENTION_DAYS` / `IMAGE_REVIEW_RETENTION_DAYS`, lalu `$unset`
  `image_key`. Event absensinya tidak dihapus.
- `storage.S3` ditulis dengan pustaka standar (AWS Signature V4, gaya path)
  supaya tidak menambah dependensi.

##### `internal/api/admin.go`

- `DeleteStudent`: kumpulkan `image_key`/`image_keys` dari `face_templates` dan
  `scan_events`, hapus fotonya dari Garage (`deleteImage`, gagal hanya
  di-log), lalu hapus `scan_events`, `face_templates`, `class_students`, dan
  `students` milik mahasiswa itu.
- `CreateLecturer`: username dinormalisasi huruf kecil dan dicek
  `^[a-z0-9._-]{3,32}$`, password ≥ `store.MinPasswordLength`, minimal satu
  kelas yang ada di `classes`, username belum dipakai (case-insensitive).
  Validasi format berjalan sebelum menyentuh MongoDB.
- `DeleteLecturer`: hanya akun ber-role `dosen`; administrator ditolak
  (`ErrNotLecturer`) agar admin tidak mengunci diri sendiri.
- Endpoint JSON `DELETE /api/v1/students/{nim}`, `GET|POST /api/v1/staff`,
  `DELETE /api/v1/staff/{id}` memakai fungsi yang sama dengan fragment.

##### `internal/api/classes_admin.go` dan pengelolaan kelas

- `ClassOptions` memuat opsi kelas dan membatasi dosen ke `class_ids`.
- `CreateClass` memvalidasi kode/nama kelas; kelas baru yang dibuat dosen
  otomatis ditambahkan ke cakupan akun pembuat.
- `DeleteClass` hanya mengizinkan administrator atau dosen untuk kelas di
  cakupan dosen. Penghapusan ditolak selama ada mahasiswa, relasi kelas,
  perangkat, riwayat scan, atau akun staf lain yang masih merujuk kelas.
- `deleted_classes` adalah tombstone agar tiga kelas bawaan yang dihapus tidak
  dibuat kembali oleh seed saat aplikasi dimulai ulang.

##### Face Recognition Service (repository terpisah)

Source Python dan instruksi build/deployment dipelihara di
[repository methics-face](https://git.algorithmics.web.id/hutao/methics-face).
Di repository ini, integrasi Go berada di `internal/api/face.go` dan
`internal/api/face_models.go`; Go memakai kontrak `/health`, `/embed`,
`/register`, serta API manajemen model dengan bearer `FACE_SERVICE_TOKEN`.
Kontrak dan alur Go dijelaskan di [integrasi Face Recognition Service](#integrasi-face-recognition-service).

##### `devices/esp32_scanner/` dan `devices/esp32cam_uart/`

- `esp32_scanner.ino`: loop non-blocking (ultrasonik tiap 100 ms, heartbeat
  tiap 15 s, buzzer, backlight); `doScan()` blocking: `camCapture()` (UART2,
  CRC32) → `postScan()` (SHA-256 + HMAC mbedTLS, multipart ditulis bertahap ke
  `WiFiClient`) → `jsonString()` mengurai respons → `showMessage()`.
  Sejak 28 Okt: `doScan()` = `camOn()` (`ON`) → `captureWindow()` (hingga
  `SCAN_MAX_FRAMES` `CAP` tersebar dalam `SCAN_WINDOW_MS`, disimpan di
  `Frames`) → `camOff()` → `postScan(frames)` (SHA-256 bertahap atas semua
  frame, satu part `image` per frame) → `showResult()` memetakan `result`
  ke layar (`tidak_terdaftar` → "Wajah tidak Terdaftar").
  Kredensial di `secrets.h` (di-gitignore). Semua fungsi didefinisikan sebelum
  dipakai. **Tipe buatan sendiri (`HttpResult`, `Frames`) wajib didefinisikan
  di atas fungsi pertama**: Arduino IDE menyisipkan prototipe otomatis semua
  fungsi tepat sebelum fungsi pertama, sehingga struct yang didefinisikan lebih
  bawah memicu error `'Frames' does not name a type`.
- `esp32cam_uart.ino`: kamera tanpa Wi-Fi; perintah `PING`/`ON`/`CAP`/`OFF`
  di UART1 (GPIO14/15), balasan `IMG <len> <crc>` + JPEG. Sensor power-down
  (PWDN) saat idle; `ON` = `esp_camera_init` + buang `WARMUP_FRAMES`, lalu
  driver terus menangkap ke 2 buffer PSRAM (`CAMERA_GRAB_LATEST`).
- `esp32cam_wifi.ino` (commissioning): fungsi kamera sama, tetapi lewat `WebServer`
  port 80 (`/ping`, `/on`, `/capture`, `/off`, header `X-Cam-Token`) dan mDNS
  `methics-cam`. Di `esp32_scanner.ino`, `CAM_LINK_WIFI` memilih implementasi
  `camPing`/`camOn`/`camOff`/`camCapture`: blok `#if CAM_LINK_WIFI` (HTTPClient,
  `camResolve()` = `CAM_HOST` atau `MDNS.queryHost`) atau blok UART. Nama dan
  tanda tangan fungsi kedua blok sama, jadi `doScan()` tidak berubah.
- Kontrak HMAC/multipart sama dengan `validScanSignature`/`readScanFrames`;
  bila salah satu berubah, ubah keduanya (lihat [panduan perangkat](#perangkat-firmware) §3, §6).

##### `devices/esp8266_ping/esp8266_ping.ino`

Sketch commissioning ESP8266 yang mengirim `POST /api/v1/devices/ping` setiap 15 detik.
Konfigurasi dan peringatan kredensial: [panduan perangkat](#perangkat-firmware) §4.

#### 5. Model data (MongoDB)

Ringkas — detail field, index, dan operasi ada di [panduan database](#database).

```text
classes        {_id: "IK24-A", name}
students       {_id: "student-241011001", nim (unik, 9 digit), name, email, class_id}
class_students {class_id, student_id}              (unik gabungan; belum dibaca query)
users          {_id, username (unik), display_name, role, password_hash,
                active, class_ids: [...], created_at}
devices        {_id: "ESP32-GED-A1", name, last_seen}
face_templates {student_id (unik), encodings [1–6 × 512], template_count, dimension,
                detection_confidences, image_key?, image_keys?, created_at,
                updated_at}                                  (ditulis Go; dokumen lama: encoding tunggal)
scan_events    {student_id, class_id (boleh ""), device_id, scanned_at, face_verified,
                liveness_passed, confidence, status, review_reason,
                frames_total, frames_with_face, frames_matched, image_key?}
```

Catatan penyimpanan waktu:

- `scan_events.scanned_at` disimpan sebagai **string RFC3339 UTC** (dari
  `captured_at` perangkat). Filter "hari ini" memakai
  `todayRangeUTC()`: tengah malam lokal server → +24 jam, diubah ke UTC dan
  dibandingkan sebagai string (`$gte`/`$lt`).
- `devices.last_seen` disimpan RFC3339 UTC; ditampilkan sebagai `HH:MM` lokal.

#### 6. Konvensi & pola

- **Sentinel error**: lapisan `api` mengembalikan `Err…`; controller memetakan
  ke pesan pengguna. Tambah sentinel baru bila ada kelas kegagalan baru.
- **Fragment adalah `{{define}}` saja**, tanpa HTML di luar blok.
- **Sukses htmx** → `HX-Trigger` (JSON) membawa event; **gagal** → 200 + pesan
  inline di form.
- **Scoping role** selalu dari `principal`, bukan dari input pengguna.
- **Ekspor minimal**: hanya yang dipakai controller yang diekspor dari `api`.

#### 7. Resep tugas umum

**Menambah halaman baru**

1. `views/pages/<nama>.html` (shell) + `views/fragments/<nama>.html` (fragment, `{{define}}`).
2. Tambahkan `"<nama>"` ke daftar `pages` di `web.go`, dan judulnya di `model/page.go`.
3. Tambahkan handler `GET /<nama>` di `main.go` (biasanya dibungkus `RequireSession`).
4. Tambahkan endpoint fragment `GET /partials/<nama>` di `partials.go` + handler di mux `Handler()`.
5. Tambahkan item sidebar di `views/partials/sidebar.html`.

**Menambah endpoint JSON staf**

1. Tambahkan query/logika sebagai fungsi ber-scope di `api/server.go`
   (terima `principal`).
2. Daftarkan route di `StaffHandler()`.
3. Bila dipakai UI juga, ekspor pembungkusnya dan pakai dari `partials.go`.

**Menambah field dokumen**

1. Tulis field baru pada `InsertOne`/`UpdateOne` terkait di `api/server.go`
   (atau `store/users.go`). Dokumen lama tidak otomatis punya field itu —
   pastikan pembaca menoleransi nilai nol, atau backfill dengan skrip `mongosh`.
2. Tambahkan field pada struct decode (`bson:"…"`) dan, bila perlu, index di
   `migrate()` pada `internal/store/database.go`.
3. Bila tampil di UI, tambahkan field struct data fragment + template.
4. Perbarui [panduan database](#database).

**Menambah aksi administrator baru**

Ikuti pola `admin.go` + `admin_partials.go`: logika & validasi di fungsi
`api.Server` (dipakai juga endpoint JSON), handler fragment memanggil
`requireAdminPartial`, lalu merender ulang region yang sama dengan pesan
`Notice`/`Error`. Untuk aksi yang menghapus data, pakai `hx-delete` +
`hx-confirm` dan sertakan filter halaman lewat `hx-include`.

**Menambah halaman log/filter baru**

Ikuti pola `/logs`: kontainer form dengan `hx-get` + `hx-trigger="load, every
15s, keyup changed delay:300ms, change"`, fragment dengan ringkasan
`hx-swap-oob`, dan query ber-scope role yang menerima nilai filter dari
`r.URL.Query()`. Escape input pencarian dengan `regexp.QuoteMeta` sebelum
dipakai di `$regex`.

#### 8. Jebakan yang mudah menimbulkan bug

1. **Token CSRF di HTML ter-escape.** `html/template` menulis `+` sebagai
   `&#43;`. Kalau membaca token dari HTML (mis. lewat skrip), HTML-decode dulu,
   atau ambil dari `meta[name="csrf-token"]` seperti `app.js`.
2. **Template di-parse sekali saat startup.** Perubahan `.html` memerlukan
   restart proses.
3. **`/partials/*` wajib di belakang session.** Handler-nya bergantung pada
   `Principal` di context; memasangnya tanpa `RequireSession` akan 401.
4. **Jangan tambahkan inline script.** CSP hanya mengizinkan `script-src 'self'`;
   `htmx.config.allowEval = false` juga menutup jalur eval.
5. **Waktu lokal vs UTC.** Pakai `todayRangeUTC()` untuk "hari ini" dan simpan
   waktu selalu sebagai RFC3339 UTC (`time.Now().UTC().Format(time.RFC3339)`).
   Format lain (mis. offset `+08:00`) merusak perbandingan string. Batas hari
   mengikuti zona waktu **server**.
6. **`.env` tidak boleh ber-BOM.** Simpan UTF-8 tanpa BOM agar parser
   `godotenv` membaca konfigurasi dengan benar.
7. **Satu `DEVICE_TOKEN` untuk semua perangkat.** Belum ada secret per
   perangkat; lihat risiko di [panduan operasional](#operasional-handoff) §Batasan.
8. **`.env` dimuat saat startup Go dan menimpa environment.** `main.go` memakai
   `godotenv.Overload()`; gunakan environment proses bila `.env` tidak
   tersedia. Atur permission `.env` agar hanya dapat dibaca operator aplikasi.
9. **Ping tanpa `device_id` memakai `ESP32-GED-A1` sebagai default.** Heartbeat
   hanya memperbarui perangkat yang sudah terdaftar dan tokennya valid.
10. **Opsi filter kelas dimuat dari database.** `Web.render` meminta daftar
    sesuai role melalui `ClassOptions`; dosen tidak melihat kelas di luar
    `Principal.ClassIDs`.
11. **Jangan menimpa filter scoping role.** Saat menambah filter dari input
    pengguna, gabungkan dengan filter role (mis. cek nilai ada di
    `principal.ClassIDs`), jangan menulis ulang key yang sama (contoh benar:
    filter `class` di `AllScanEvents`).
12. **Keanggotaan kelas = `students.class_id`.** `class_students` ditulis
    tetapi tidak dibaca; jangan mengandalkannya tanpa memperbarui query.
13. **Foto bukan bagian wajib alur absensi.** Jangan membuat scan/enrollment
    gagal karena S3; ikuti pola `uploadImage` (log + lanjut).
14. **Ganti model recognition = enrollment ulang.** Embedding lama tidak
    sebanding dengan model baru; foto enrollment di Garage bisa dipakai untuk
    membuat ulang template (belum diotomasi).
15. **NIM wajib 9 digit angka** (`api.ValidNIM`, mis. `241011001`). Validasi
    ada di `CreateStudent`, jadi endpoint JSON dan form htmx memakai aturan
    yang sama; pola `pattern` di `mahasiswa_form.html` hanya bantuan browser.
    Mahasiswa contoh `IK24…` dari seed lama tidak lolos aturan ini dan tidak
    dibuat ulang lagi.
16. **`hx-include` pada `DELETE` dikirim sebagai query string**, jadi handler
    membaca filter dengan `r.FormValue` (bukan `r.PostFormValue`).


## Database

### Basis Data (MongoDB)

> Status: **berlaku** — sesuai `internal/store/database.go`, `internal/store/users.go`,
> dan query di `internal/api/server.go`, `face.go`, `images.go`, `admin.go`.
> Terakhir diselaraskan: 28 Oktober 2026.
> Versi lama proyek memakai SQLite; semua rujukan ke `DATABASE_PATH`,
> `data/attendance.db`, tabel `user_classes`, atau `date(…, 'localtime')`
> sudah **tidak berlaku**.

#### 1. Koneksi

| Variabel | Default | Keterangan |
| --- | --- | --- |
| `MONGODB_URI` | — | wajib dikonfigurasi; URI driver resmi `go.mongodb.org/mongo-driver` v1.17 |
| `MONGODB_DATABASE` | `attendance` | nama database; dibuat otomatis saat pertama ditulisi |

`store.Open` melakukan, berurutan:

1. `mongo.Connect` lalu `Ping` dengan batas waktu **8 detik**. Gagal → server
   berhenti dengan pesan galat yang tidak mencetak URI koneksi.
2. `migrate` — membuat index (idempoten; aman dijalankan setiap startup).
3. `seed` — upsert kelas dan perangkat memakai `$setOnInsert`, sehingga
   **tidak menimpa** data yang sudah diubah (lihat §5). Kelas yang dihapus
   dicatat di `deleted_classes` agar tidak muncul kembali saat restart.
   Mahasiswa tidak di-seed.

Nilai `MONGODB_*` dibaca dari `.env` dengan `godotenv.Overload` (menimpa
environment). Baris log `connected to MongoDB (database: …)` menunjukkan nama
database yang dipakai tanpa mencatat URI atau kredensialnya.

Koneksi ditutup saat shutdown (`Database.Close`, batas 5 detik).

#### 2. Koleksi

ID dokumen memakai string yang bermakna (bukan `ObjectId`), kecuali
`scan_events`, `class_students`, dan `face_templates` yang memakai `_id`
otomatis.

##### `classes`

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `_id` | string | kode kelas, mis. `IK24-A` (huruf besar) |
| `name` | string | nama tampilan, mis. `Ilmu Komputer A` |
| `created_at` | date | waktu kelas dibuat melalui UI (opsional untuk data seed) |
| `created_by` | string | ID akun staf pembuat kelas (opsional untuk data seed) |

##### `deleted_classes`

Tombstone untuk kelas seed yang dihapus melalui UI. `_id` adalah kode kelas;
`deleted_at` dan `deleted_by` mencatat waktu dan akun penghapus. Koleksi ini
mencegah seed menghidupkan kembali kelas awal pada startup berikutnya.

##### `students`

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `_id` | string | `student-<nim>`, mis. `student-241011001` |
| `nim` | string | **9 digit angka** (mis. `241011001`), **unik** |
| `name` | string | nama lengkap |
| `email` | string | saat ini selalu `""` |
| `class_id` | string | kelas utama → `classes._id` |

`students.class_id` adalah **sumber kebenaran** keanggotaan kelas yang dipakai
query (ringkasan kelas, roster, cek anggota kelas saat scan).

##### `class_students`

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `class_id` | string | → `classes._id` |
| `student_id` | string | → `students._id` |

Relasi ini ditulis saat `CreateStudent`, tetapi **belum dibaca**
oleh query mana pun. Ia disiapkan untuk mahasiswa yang mengikuti lebih dari
satu kelas. Bila kelak dipakai, pastikan kedua sumber tetap sinkron.

##### `users` (akun staf)

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `_id` | string | 32 karakter hex acak (`newID`) |
| `username` | string | **unik** (index biasa, case-sensitive); pencarian login memakai regex case-insensitive |
| `display_name` | string | nama tampilan |
| `role` | string | `administrator` atau `dosen` |
| `password_hash` | binary | bcrypt (`DefaultCost`); password minimal 12 karakter (`store.MinPasswordLength`) |
| `active` | bool | `false` → login & session ditolak |
| `class_ids` | array string | kelas yang ditugaskan (untuk dosen); diisi dari centang kelas di form `/akses` |
| `created_at` | date | UTC |

Penugasan kelas dosen disimpan langsung di `users.class_ids` (pengganti tabel
`user_classes` di versi SQLite). Middleware membacanya ulang di **setiap
request**, jadi perubahan langsung berlaku tanpa logout.

Akun dibuat oleh dua jalur: bootstrap `.env` (`BOOTSTRAP_ADMIN_*`,
`BOOTSTRAP_LECTURER_*`; bootstrap hanya membuat akun dan tidak mereset
kredensial akun yang sudah ada), form "Buat akun dosen" di `/akses`
(`CreateLecturer`: username huruf kecil, role `dosen`), dan `POST /api/v1/staff`.
Menghapus akun dosen di `/akses` menghapus dokumennya; sesi dosen itu
langsung tidak berlaku.

> Karena index unik `username` bersifat case-sensitive sedangkan login
> case-insensitive, `Admin` dan `admin` bisa tersimpan sebagai dua dokumen
> lewat `CreateUser`. `EnsureUser` dan `CreateLecturer` (form
> `/akses`) sudah memeriksa username tanpa membedakan huruf besar/kecil.

##### `devices`

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `_id` | string | ID perangkat, mis. `ESP32-GED-A1` |
| `name` | string | nama tampilan |
| `last_seen` | string | RFC3339 UTC; diperbarui oleh scan atau heartbeat terautentikasi |

Status "online" di dashboard = ada perangkat dengan `last_seen` < **2 menit**
yang lalu (diambil satu dokumen dengan `last_seen` terbaru).

> Endpoint heartbeat `/api/v1/devices/ping` memerlukan `DEVICE_TOKEN` dan
> hanya memperbarui perangkat yang sudah terdaftar. `/api/v1/health` bersifat
> read-only dan tidak mengubah `last_seen`.

##### `face_templates`

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `student_id` | string | → `students._id`, **unik** (satu dokumen per mahasiswa) |
| `encodings` | array of array double | 1–6 embedding ArcFace 512 dimensi (ternormalisasi L2), satu per foto pose |
| `template_count` | int | jumlah vektor di `encodings` |
| `dimension` | int | panjang tiap vektor |
| `detection_confidences` | array double | confidence deteksi wajah per foto |
| `created_at` | date | enrollment pertama (UTC) |
| `updated_at` | date | enrollment terakhir (UTC) |
| `image_keys` | array string, opsional | kunci semua foto enrollment di bucket S3 (Garage); hanya ada bila penyimpanan foto aktif |
| `image_key` | string, opsional | kunci foto pertama (dipakai tombol **Lihat foto**) |
| `encoding`, `detection_confidence` | lama | format sebelum 28 Okt 2026 (satu foto); tetap dibaca sebagai satu pose dan dihapus saat enrollment ulang |

Ditulis oleh **Go** (`enrollEmbeddings` di `internal/api/face.go`) dari
embedding yang dikembalikan Face Recognition Service `POST /register`;
service tersebut tidak mengakses MongoDB. Source service ada di
[repository methics-face](https://git.algorithmics.web.id/hutao/methics-face).
Saat scan, Go memuat semua dokumen koleksi
ini dan menghitung cosine similarity terhadap setiap pose (skor mahasiswa =
pose terbaik). Lihat `bagian Integrasi Face Service`.

##### `scan_events`

| Field | Tipe | Keterangan |
| --- | --- | --- |
| `student_id` | string | mahasiswa yang cocok (suara frame terbanyak); `""` bila tidak dikenali |
| `class_id` | string | kelas tetap perangkat, atau — bila perangkat mengirim `class_id` kosong — kelas mahasiswa yang cocok; `""` untuk wajah tidak dikenal pada perangkat kelas otomatis |
| `device_id` | string | perangkat pengirim |
| `scanned_at` | string | `captured_at` perangkat, diubah ke **RFC3339 UTC** |
| `face_verified` | bool | ada mahasiswa yang cocok (≥ 1 frame dengan skor ≥ `FACE_SIM_THRESHOLD`) |
| `liveness_passed` | bool | klaim perangkat (bukan deteksi biologis) |
| `confidence` | number | cosine similarity terbaik terhadap `face_templates` (0 bila tidak ada wajah) |
| `frames_total` | int | jumlah frame yang dikirim perangkat (1–6) |
| `frames_with_face` | int | frame yang diproses dan berisi wajah (Go berhenti lebih awal begitu cukup frame cocok) |
| `frames_matched` | int | frame yang cocok dengan `student_id`; ≥ `SCAN_MIN_MATCH_FRAMES` → `Hadir` |
| `status` | string | `Hadir` / `Perlu ditinjau` / `Ditolak` |
| `review_reason` | string | alasan untuk status selain `Hadir` |
| `image_key` | string, opsional | kunci satu frame bukti (skor terbaik) di bucket S3; dihapus (`$unset`) saat foto melewati masa retensi |

#### 3. Index

Dibuat oleh `migrate()`:

| Koleksi | Index | Opsi |
| --- | --- | --- |
| `users` | `{username: 1}` | unique |
| `students` | `{nim: 1}` | unique |
| `students` | `{class_id: 1}` | — |
| `class_students` | `{class_id: 1, student_id: 1}` | unique |
| `face_templates` | `{student_id: 1}` | unique |
| `scan_events` | `{scanned_at: -1}` | — |
| `scan_events` | `{class_id: 1, scanned_at: -1}` | — |
| `scan_events` | `{student_id: 1, scanned_at: -1}` | — |
| `scan_events` | `{status: 1}` | — |
| `scan_events` | `{image_key: 1}` | sparse |
| `face_templates` | `{image_key: 1}` | sparse |
| `face_templates` | `{image_keys: 1}` | sparse, multikey |

#### 4. Waktu dan "hari ini"

- `scan_events.scanned_at` dan `devices.last_seen` disimpan sebagai **string
  RFC3339 UTC** (`2026-10-21T00:15:00Z`), bukan tipe `Date` BSON.
- Filter "hari ini" dihitung di Go oleh `todayRangeUTC()`: tengah malam
  **zona waktu lokal server** sampai +24 jam, dikonversi ke UTC, lalu
  dibandingkan sebagai string (`$gte` / `$lt`). Perbandingan leksikografis ini
  benar selama semua nilai berformat RFC3339 UTC yang seragam (akhiran `Z`).
- Akibatnya zona waktu server menentukan batas hari. Untuk ITBJH (WITA,
  UTC+8) jalankan server dengan zona waktu `Asia/Makassar` (atau set `TZ`).
- Jam di tampilan memakai `time.Local` (`formatTime` → `15:04`).

Jangan menulis `scanned_at` dalam format lain (mis. dengan offset `+08:00`):
data itu akan salah masuk/keluar dari rentang "hari ini".

#### 5. Data seed

| Jenis | Data |
| --- | --- |
| Kelas | `IK24-A` Ilmu Komputer A, `IK24-B` Ilmu Komputer B, `IK24-C` Ilmu Komputer C |
| Mahasiswa | **tidak ada** — didaftarkan manual (NIM 9 digit, mis. `241011001`) |
| Perangkat | `ESP32-GED-A1` ESP32 Gedung A, `ESP32-GED-B1` ESP32 Gedung B |
| Akun staf | **tidak ada** — dibuat dari `BOOTSTRAP_*` (lihat `panduan utama`) |
| Template wajah / scan | **tidak ada** |

Seed berjalan di setiap startup tetapi hanya menyisipkan kelas dan perangkat
yang belum ada. Tiga kelas awal yang sengaja dihapus melalui UI diberi
tombstone di `deleted_classes`, jadi seed tidak memulihkannya. Versi lama juga
men-seed mahasiswa `IK24001`–`IK24005`; bila
masih ada di database, hapus sekali lewat halaman Mahasiswa (tidak akan
dibuat lagi), atau lewat `mongosh`:

```javascript
db.students.find({ nim: /^IK24/ })          // cek dulu
// lalu hapus lewat tombol hapus di dashboard agar template, foto, dan scan ikut terhapus
```

#### 5a. Penghapusan mahasiswa

`DELETE /partials/mahasiswa/{nim}` (tombol hapus di halaman Mahasiswa) dan
`DELETE /api/v1/students/{nim}` menjalankan `DeleteStudent`
(`internal/api/admin.go`):

1. Mengumpulkan `image_key`/`image_keys` dari `face_templates` dan `image_key` dari `scan_events` mahasiswa
   itu, lalu menghapus fotonya dari Garage (gagal hanya dicatat di log).
2. Menghapus dokumen `scan_events`, `face_templates`, `class_students`, dan
   `students` milik mahasiswa itu (berurutan; tidak dalam transaksi).

Riwayat absensi mahasiswa tersebut ikut hilang dan tidak bisa dikembalikan
kecuali dari backup (§7).

#### 6. Operasi umum (mongosh)

```javascript
use methics   // sesuaikan dengan MONGODB_DATABASE

// Melihat akun staf (tanpa hash password)
db.users.find({}, { username: 1, role: 1, class_ids: 1, active: 1 })

// Menugaskan kelas ke dosen
db.users.updateOne({ username: "rina" }, { $set: { class_ids: ["IK24-A", "IK24-C"] } })

// Menonaktifkan akun staf (sesi aktifnya ikut ditolak pada request berikutnya)
db.users.updateOne({ username: "rina" }, { $set: { active: false } })

// Menambah kelas baru (pilihan kelas di UI diambil dari database)
db.classes.insertOne({ _id: "IK24-D", name: "Ilmu Komputer D" })

// Mendaftarkan perangkat baru secara eksplisit
db.devices.insertOne({ _id: "ESP32-GED-C1", name: "ESP32 Gedung C" })

// Melihat jumlah pose dan kunci foto tiap template (tanpa vektor 512-d)
db.face_templates.find({}, { student_id: 1, template_count: 1, dimension: 1, image_keys: 1 })

// Template format lama (satu foto) yang sebaiknya di-enroll ulang
db.face_templates.find({ encoding: { $exists: true } }, { student_id: 1 })

// Scan hari ini yang perlu ditinjau
db.scan_events.find({ status: "Perlu ditinjau" }).sort({ scanned_at: -1 }).limit(20)
```

Membuat/menghapus akun dosen, menghapus mahasiswa (beserta data biometrik),
serta menambah/menghapus kelas sudah tersedia di UI. Dosen yang membuat kelas
otomatis mendapat penugasan ke kelas tersebut. Hapus kelas ditolak jika masih
dipakai mahasiswa, perangkat, riwayat scan, atau akun staf lain. Mengubah
penugasan dosen dan mendaftarkan perangkat belum ada di UI. Jangan menghapus
`face_templates` langsung bila fotonya ada di Garage; gunakan hapus mahasiswa
agar foto ikut terhapus.

#### 7. Backup & pemulihan

```bash
# Backup (arsip terkompresi)
mongodump --uri="$MONGODB_URI" --db="$MONGODB_DATABASE" --archive=methics-$(date +%F).gz --gzip

# Pemulihan
mongorestore --uri="$MONGODB_URI" --archive=methics-2026-10-26.gz --gzip --drop
```

Backup MongoDB tidak mencakup foto di Garage; cadangkan bucket terpisah bila
foto perlu dipulihkan. `face_templates` berisi data biometrik: simpan arsip backup terenkripsi,
batasi aksesnya, dan tetapkan masa retensi. Panduan hardening server MongoDB
(user aplikasi, `authorization`, `bindIp`, firewall, TLS) ada di `panduan utama`.

#### 8. Mengubah skema

MongoDB tidak memerlukan `ALTER TABLE`, tetapi dokumen lama tidak otomatis
punya field baru:

1. Tambahkan field pada insert/update di `internal/api/server.go` (atau
   `internal/store/users.go`).
2. Pastikan pembaca menoleransi field yang hilang (nilai nol pada struct Go).
3. Bila perlu index, tambahkan di `migrate()`; `CreateOne` idempoten untuk
   definisi yang sama, tetapi **gagal** bila index dengan nama sama sudah ada
   dengan opsi berbeda — hapus index lama dulu.
4. Untuk backfill data lama, tulis skrip `mongosh` terpisah dan catat di
   `panduan operasional`. Belum ada mekanisme migrasi berversi.


## Perangkat & Firmware

### Perangkat (ESP32 / ESP32-CAM / ESP8266)

> Status: **[SEBAGIAN]**. Firmware perangkat absensi tersedia; commissioning
> fisik masih perlu dituntaskan sebelum pemasangan. Terakhir diperbarui 28 Oktober 2026
> (scan berbasis stream: kamera menyala saat ada orang, ±2 detik multi-frame).

#### 1. Ringkasan

| Folder | Board | Peran |
| --- | --- | --- |
| `devices/esp32_scanner/` | ESP32 Dev Module | **perangkat absensi**: ultrasonik → kamera ON → ±2 detik frame → `POST /api/v1/scans` (multi-frame) → hasil di layar + buzzer; heartbeat |
| `devices/esp32cam_uart/` | AI Thinker ESP32-CAM | **kamera** untuk `esp32_scanner`; mati saat idle, menyala + stream atas perintah UART, tanpa Wi-Fi |
| `devices/esp32cam_wifi/` | AI Thinker ESP32-CAM | kamera Wi-Fi alternatif untuk commissioning saja (§2.6); jangan digunakan untuk pemasangan permanen |
| `devices/algorithmics_engine_ultrasonic_buzzer/` | ESP32 | firmware commissioning layar, ultrasonik, dan buzzer |
| `devices/esp32cam_stream/` | ESP32-CAM | firmware commissioning arah/fokus kamera; **jangan dipakai di jaringan produksi** |
| `devices/esp8266_ping/` | ESP8266 | firmware commissioning heartbeat |

| Fungsi | Endpoint | Auth |
| --- | --- | --- |
| Heartbeat / status online | `GET` atau `POST /api/v1/devices/ping` | bearer `DEVICE_TOKEN` wajib |
| Cek kesehatan backend | `GET /api/v1/health` | tidak ada; read-only |
| Kirim scan wajah | `POST /api/v1/scans` | bearer `DEVICE_TOKEN` + HMAC-SHA256 + timestamp |

Ketiganya dipasang di router luar (`cmd/server/main.go`), sehingga **tidak**
melewati session maupun CSRF browser. Dashboard menampilkan perangkat
"Terhubung" bila ada perangkat dengan `last_seen` kurang dari **2 menit**.

#### 2. Perangkat absensi ESP32 (`esp32_scanner` + `esp32cam_uart`)

```text
HC-SR04 ──► ESP32 utama ──UART 460800 "ON" / "CAP"×4 / "OFF"──► ESP32-CAM (stream QVGA)
TFT, buzzer ◄──┘   │  ◄──────── JPEG + CRC32 per frame ────────┘
                   └── Wi-Fi ──► Go  POST /api/v1/scans (1–6 frame, Bearer + HMAC)
                                     │   └─► Python /embed per frame → cosine vs MongoDB
                                     POST /api/v1/devices/ping (tiap 15 s)
```

Rangkaian **tidak berubah** dari versi sebelumnya; yang berubah hanya
firmware kedua board dan logika scan di Go.

##### 2.1 Wiring

| Dari | Ke | Catatan |
| --- | --- | --- |
| ESP32-CAM **GPIO15** (TX1) | ESP32 **GPIO16** (RX2) | data foto |
| ESP32-CAM **GPIO14** (RX1) | ESP32 **GPIO17** (TX2) | perintah |
| ESP32-CAM **GND** | ESP32 **GND** | **wajib** disatukan |
| ESP32-CAM **5V** | catu 5 V ≥ 2 A | jangan dari pin 3V3 ESP32 utama; kamera + Wi-Fi butuh arus puncak tinggi |
| HC-SR04 TRIG / ECHO | GPIO26 / GPIO27 | ECHO lewat pembagi tegangan 1k + 2k (5 V → 3,3 V) |
| Buzzer (+) | GPIO22 | modul *low trigger*: ubah `BUZZER_ON_LEVEL` ke `LOW` |
| TFT ILI9341 | sesuai `User_Setup.h` TFT_eSPI | backlight `TFT_BL` (default GPIO21) |

- GPIO14/15 pada ESP32-CAM adalah pin kartu SD — **jangan pasang kartu SD**.
- GPIO16/17 harus tidak dipakai di `User_Setup.h` TFT_eSPI. Pada modul
  **ESP32-WROVER**, GPIO16/17 dipakai PSRAM; ganti `CAM_RX_PIN`/`CAM_TX_PIN`.
- UART0 ESP32-CAM (GPIO1/3) tetap untuk upload & log, jadi kabel ke ESP32 utama
  tidak perlu dicabut saat flashing.

##### 2.2 Perilaku

1. Saat start: Wi-Fi → sinkron jam NTP (UTC) → `PING` ke kamera → heartbeat
   pertama. Layar menampilkan "Siap" atau "Periksa" (Wi-Fi/jam/kamera mana
   yang gagal). Kamera dalam keadaan **mati** (power-down) sampai ada orang.
2. Objek ≤ `DETECT_CM` (20 cm) terdeteksi 2× berturut-turut → layar menyala,
   "Kamera menyala", bip pendek, ESP32 mengirim `ON` ke kamera. Kamera
   menyala (±0,5–1 detik termasuk pemanasan exposure) lalu terus menangkap
   frame ke buffer PSRAM.
3. **Jendela scan ±2 detik** (`SCAN_WINDOW_MS`): layar "Memindai..." dengan
   garis progres; ESP32 mengambil hingga `SCAN_MAX_FRAMES` (4) frame JPEG
   QVGA (±8–15 KB) yang tersebar merata, lalu mengirim `OFF` ke kamera.
4. Semua frame dikirim **dalam satu request** ke `POST /api/v1/scans`
   (field `image` berulang) dengan tanda tangan HMAC (§6). Go mencocokkan
   setiap frame dengan semua wajah terdaftar (semua pose enrollment):
   - wajah yang sama cocok di **≥ 2 frame** (`SCAN_MIN_MATCH_FRAMES` di
     server) → **Hadir**, tercatat di kelas mahasiswa itu (`CLASS_ID` kosong =
     kelas otomatis) dan langsung tampil di dashboard;
   - ada wajah tetapi tidak cocok dengan siapa pun → **Ditolak**, layar
     **"Wajah tidak Terdaftar"**;
   - tidak ada wajah di semua frame → **Ditolak**, "Wajah tidak terdeteksi";
   - cocok di 1 frame saja, atau mahasiswa bukan anggota `CLASS_ID` tetap
     perangkat → **Perlu ditinjau**.
   `liveness_passed` tetap dikirim `false`; kecocokan beberapa frame
   **bukan** anti-spoof (foto cetak di depan kamera bisa lolos), lihat §7.
5. Hasil di layar + pola buzzer (dari field `result` respons):

   | `result` | Layar | Buzzer |
   | --- | --- | --- |
   | `hadir` | **HADIR** (hijau) + nama + NIM + kelas | 1 bip panjang |
   | `tidak_terdaftar` | **Wajah tidak Terdaftar** (merah) | 3 bip pendek |
   | `wajah_tidak_terdeteksi` | **Wajah tidak terdeteksi** (oranye) + "Hadapkan wajah ke kamera" | 2 bip |
   | `perlu_ditinjau` | **DITINJAU** (oranye) + nama + alasan | 2 bip |
   | error (HTTP 4xx/5xx, kamera, Wi-Fi, jam) | **GAGAL** + kode + pesan | 3 bip pendek |

6. Scan berikutnya baru terjadi setelah objek menjauh (~0,5 s) **dan** lewat
   `SCAN_COOLDOWN_MS` (6 s), agar satu orang tidak terpindai berulang.
7. Heartbeat `POST /api/v1/devices/ping` tiap 15 s.
8. Respons `401 invalid_signature` / `400 stale_capture` memicu sinkron ulang
   jam pada scan berikutnya.

Perkiraan waktu dari orang berdiri di depan alat sampai hasil tampil:
kamera ON ±1 s + jendela scan ±2 s + kirim & cocokkan ±0,5–1,5 s (Go
berhenti lebih awal begitu 2 frame cocok).

##### 2.3 Konfigurasi

`devices/esp32_scanner/secrets.h` (salin dari `secrets.example.h`; di-gitignore):

| Konstanta | Isi |
| --- | --- |
| `WIFI_SSID`, `WIFI_PASSWORD` | Wi-Fi 2,4 GHz yang menjangkau server |
| `DEVICE_TOKEN` | sama persis dengan `DEVICE_TOKEN` di `.env` backend |

Bagian atas `esp32_scanner.ino`:

| Konstanta | Default | Keterangan |
| --- | --- | --- |
| `SERVER_HOST`, `SERVER_PORT` | `10.229.23.195`, `8080` | IP komputer yang menjalankan Go |
| `DEVICE_ID` | `ESP32-GED-A1` | harus ada di koleksi `devices` (seed) |
| `CLASS_ID` | `""` (kosong) | kosong = **kelas otomatis** (kehadiran dicatat di kelas mahasiswa yang cocok); isi kode kelas huruf besar (mis. `IK24-A`) bila alat khusus satu kelas |
| `LIVENESS_PASSED` | `false` | lihat §2.2 butir 4 |
| `SCAN_WINDOW_MS`, `SCAN_MAX_FRAMES` | 2000, 4 | lama jendela scan dan jumlah frame (maks 6, batas server) |
| `DETECT_CM`, `SCAN_COOLDOWN_MS`, `RESULT_SHOW_MS` | 20, 6000, 6000 | jarak deteksi & jeda |
| `CAM_BAUD`, `MAX_IMAGE_BYTES` | 460800, 24000 | **samakan** dengan `LINK_BAUD`, `MAX_JPEG_BYTES` di `esp32cam_uart` |
| `CAM_ON_TIMEOUT_MS`, `CAM_TIMEOUT_MS` | 5000, 4000 | batas kamera menyala dan batas satu frame |

Di `esp32cam_uart.ino`: `FRAME_SIZE` (QVGA 320×240), `JPEG_QUALITY` (10),
`WARMUP_FRAMES` (4 frame awal dibuang agar exposure stabil), `IDLE_OFF_MS`
(15 s, kamera mati sendiri bila tidak ada perintah), `USE_FLASH` (0; 1 = LED
flash menyala selama kamera ON, berguna di ruangan gelap).

Sisi server (`.env` Go): `SCAN_MIN_MATCH_FRAMES` (default 2) dan
`FACE_SIM_THRESHOLD` (default 0.45).

Memori: 4 frame × ≤ 24 KB ≈ 100 KB RAM di ESP32 utama (WROOM tanpa PSRAM).
Bila `SCAN_MAX_FRAMES` atau `MAX_IMAGE_BYTES` dinaikkan, cek `maxblok` pada
perintah `status`; firmware berhenti mengambil frame bila heap tersisa
< `HEAP_RESERVE` (40 KB).

##### 2.4 Build & upload (Arduino IDE)

1. Board manager URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json`,
   pasang paket **esp32** (diuji kompilasi dengan 3.0.7).
2. Library **TFT_eSPI** (Bodmer) dengan `User_Setup.h` yang sama seperti sketch
   `algorithmics_engine_ultrasonic_buzzer`, dan `#define LOAD_GFXFF` aktif.
3. **Kamera dulu** (upload terpisah dari ESP32 utama): buka
   `devices/esp32cam_uart`, board *AI Thinker ESP32-CAM*.
   - Dengan **ESP32-CAM-MB** (papan uploader USB): pasang kamera di MB,
     pilih port COM MB, upload. MB mengatur mode boot sendiri; bila muncul
     `Failed to connect`, tahan tombol **IO0** di MB, tekan **RST**, lepas IO0
     saat tulisan `Connecting...`.
   - Dengan adaptor USB-serial biasa: hubungkan GPIO0 ke GND saat upload,
     lepas, tekan RST.
   - Serial Monitor 115200 harus menampilkan
     `[CAM] UART1 460800 baud, … kamera OK (idle: mati)`, dan `[CAM] ON dalam … ms (PSRAM=1)`
     setiap kali ada scan.
   - Setelah itu **lepas kamera dari MB**: pin GPIO14/15 tertutup oleh MB,
     jadi kabel ke ESP32 utama (§2.1) dipasang langsung ke pin kamera, dengan
     catu 5 V terpisah. MB baru dipakai lagi saat upload ulang (lepas kabel
     UART dulu).
4. **ESP32 utama**: lewat kabel USB-nya sendiri, buka `devices/esp32_scanner`,
   buat `secrets.h`, board *ESP32 Dev Module*, upload, Serial Monitor 115200.

##### 2.5 Commissioning bertahap

Ketik di Serial Monitor ESP32 utama (akhiri dengan newline):

| Perintah | Pemeriksaan commissioning | Hasil yang diharapkan |
| --- | --- | --- |
| `status` | Wi-Fi, jam, kamera, memori | `WiFi=OK … jam=… (sinkron) kamera=OK` |
| `cam` | kabel UART | `[Cam] PONG` |
| `camon` / `camoff` | daya & sensor kamera | `[Cam] ON OK` / `[Cam] OFF` |
| `ping` | server & token | `[Ping] HTTP 200`; dashboard menjadi "Terhubung" |
| `scan` | alur lengkap tanpa sensor | `[Scan] kamera ON …`, `[Scan] frame 1..4: … byte`, lalu `[Scan] HTTP 202 … {"result":"hadir",…}` |

`DEVICE_ID` harus ada di koleksi `devices` database yang dipakai server
(seed membuat `ESP32-GED-A1`/`B1` di setiap start), dan bila `CLASS_ID`
diisi, kelasnya harus ada; kalau tidak, scan ditolak `403 unknown_device` /
`400 unknown_class`.

Urutan disarankan: `cam` → `camon`/`camoff` → `ping` → enrollment akun
mahasiswa commissioning → `scan` → cek halaman Log (status, skor, foto
bila Garage aktif) → cek sensor ultrasonik → verifikasi wajah tidak terdaftar
ditolak. Terminal Go menulis log
`scan ESP32-GED-A1: Hadir (hadir) frame=4 wajah=… cocok=… skor=…`.

##### 2.6 Commissioning kamera lewat Wi-Fi (tanpa kabel antar-board)

Alat commissioning ini dapat dipakai bila ESP32-CAM belum bisa dicatu dan dikabel bersama ESP32 utama
(mis. belum ada kabel/catu 5 V terpisah). ESP32-CAM tetap terpasang di
**ESP32-CAM-MB** dengan USB-nya sendiri, lalu tersambung ke Wi-Fi yang sama
dengan ESP32 utama. **Tidak ada kabel apa pun** di antara kedua board.

```text
HC-SR04 ──► ESP32 utama ──Wi-Fi (LAN) POST /on, GET /capture ×4, POST /off──► ESP32-CAM (di MB, USB sendiri)
TFT, buzzer ◄──┘   └── Wi-Fi ──► Go  POST /api/v1/scans (sama seperti mode UART)
```

Alur scan, frame, dan hasil di layar **sama persis** dengan mode UART (§2.2);
yang berbeda hanya jalur ESP32 utama ↔ kamera.

**Langkah:**

1. **Kamera**: buka `devices/esp32cam_wifi`, salin `secrets.example.h` menjadi
   `secrets.h`, isi `WIFI_SSID`, `WIFI_PASSWORD` (Wi-Fi yang sama dengan ESP32
   utama, 2,4 GHz) dan `CAM_TOKEN` (teks acak bebas). Upload lewat MB
   (board *AI Thinker ESP32-CAM*). Biarkan kamera tetap di MB dan tetap
   tercolok USB (lebih stabil dengan charger HP 5V 2A daripada port laptop).
2. Serial Monitor kamera (115200) menampilkan
   `[WiFi] IP kamera: 192.168.x.x` dan `[mDNS] http://methics-cam.local`.
3. **ESP32 utama**: tambahkan ke `devices/esp32_scanner/secrets.h`
   ```c
   #define CAM_TOKEN "token-yang-sama-dengan-kamera"
   ```
   Di `esp32_scanner.ino` pastikan `#define CAM_LINK_WIFI 1`. Bila mDNS tidak
   jalan di jaringanmu (sering terjadi di hotspot HP / Wi-Fi kampus), isi
   `#define CAM_HOST "192.168.x.x"` dengan IP dari langkah 2. Upload.
4. Uji di Serial Monitor ESP32 utama: `cam` → `[Cam] PONG`, `camon` /
   `camoff`, lalu `scan`.

| Endpoint kamera | Fungsi |
| --- | --- |
| `GET /ping` | `PONG psram=<0\|1> cam=<on\|off>` |
| `POST /on` | kamera menyala + pemanasan exposure → `ON` |
| `GET /capture` | frame JPEG terbaru (`image/jpeg`, ≤ 24 KB); kamera menyala dulu bila mati |
| `POST /off` | kamera power-down → `OFF` |

Semua endpoint wajib header `X-Cam-Token: <CAM_TOKEN>` (salah → `401`,
layar "Token kamera salah"). Kamera tetap mati sendiri setelah 15 s tanpa
permintaan.

**Batasan commissioning:** jalur kamera memakai HTTP biasa di LAN (token terkirim
tanpa enkripsi) dan bergantung pada kualitas Wi-Fi; latensi per frame
±100–300 ms. Untuk pemasangan permanen kembali ke kabel UART
(`CAM_LINK_WIFI 0` + `esp32cam_uart`) dengan catu 5 V bersama dan GND
disatukan.

#### 3. Protokol UART kamera

UART 8N1, 460800 baud, satu permintaan per waktu:

| Arah | Pesan |
| --- | --- |
| ESP32 → kamera | `PING\n` → kamera `PONG psram=<0\|1> cam=<on\|off>\n` |
| ESP32 → kamera | `ON\n` → kamera menyala, membuang `WARMUP_FRAMES` frame, lalu `ON\n` |
| ESP32 → kamera | `CAP\n` → kamera `IMG <panjang> <crc32hex>\n` + `<panjang>` byte JPEG (frame terbaru dari stream; bila kamera mati, menyala dulu) |
| ESP32 → kamera | `OFF\n` → kamera power-down, lalu `OFF\n` |
| kamera → ESP32 | `ERR <alasan>\n` (`camera_init`, `capture`, `too_big <n>`, `unknown_command`) |
| kamera → ESP32 | `READY\n` saat start (diabaikan ESP32 utama) |

Dengan PSRAM, driver kamera memakai 2 buffer dan `CAMERA_GRAB_LATEST`, jadi
selama ON sensor terus menangkap (stream) dan setiap `CAP` langsung
mendapat frame terbaru tanpa jeda. Kamera mati sendiri setelah
`IDLE_OFF_MS` (15 s) tanpa perintah sebagai pengaman.

CRC-32 = CRC IEEE (sama dengan `zlib.crc32`). ESP32 utama menolak frame bila
panjang tidak lengkap dalam `CAM_TIMEOUT_MS` (4 s) atau CRC tidak cocok
("Foto rusak (CRC)"); frame yang gagal dilewati dan scan memakai frame
lainnya. Bila kabel panjang/berisik dan CRC sering gagal, turunkan baud di
**kedua** sketch (mis. 230400). Frame lebih besar dari 24 KB diulang dengan
kualitas JPEG 18; bila masih terlalu besar, kamera membalas `ERR too_big`.

#### 4. Firmware commissioning heartbeat ESP8266

Berkas: `devices/esp8266_ping/esp8266_ping.ino`. Mengirim `POST` ke
`SERVER_URL` setiap `PING_INTERVAL_MS` (15 s) dengan header
`Authorization: Bearer <DEVICE_TOKEN>`, `X-Device-Id`, dan body
`device_id=<DEVICE_ID>`. Board: paket **esp8266**
(`http://arduino.esp8266.com/stable/package_esp8266com_index.json`), mis.
*NodeMCU 1.0*. Setelah `esp32_scanner` berjalan, ESP8266 tidak diperlukan lagi
(keduanya memakai `DEVICE_ID` yang sama).

##### ⚠️ Jangan commit kredensial

Sketch ESP8266 dan `esp32cam_stream` menulis SSID/password Wi-Fi (dan ESP8266
juga `DEVICE_TOKEN`) langsung di kode. Versi lama sketch ESP8266 **sudah
ter-commit** (commit `9af21ce`), sehingga nilainya ada di riwayat Git:

1. **Ganti `DEVICE_TOKEN`** di `.env` dan perangkat, serta password Wi-Fi.
2. Sebelum meng-commit `esp32cam_stream` atau `esp8266_ping`, kosongkan nilai
   rahasianya atau pindahkan ke `secrets.h` seperti `esp32_scanner`
   (`secrets.h` sudah di-gitignore).

#### 5. Endpoint heartbeat

`GET|POST /api/v1/devices/ping`

ID perangkat dibaca berurutan dari: field form `device_id` → query
`?device_id=` → header `X-Device-Id` → **default `ESP32-GED-A1`**.

Bearer `DEVICE_TOKEN` wajib pada setiap request; token salah → `401 invalid_token`.

Efek: memperbarui `last_seen` (RFC3339 UTC) pada perangkat yang sudah
terdaftar. ID yang tidak terdaftar menghasilkan `403 unknown_device`; ping
tidak membuat dokumen perangkat.

Respons `200`:

```json
{
  "status": "ok",
  "device_id": "ESP32-GED-A1",
  "last_seen": "2026-10-21T00:15:00Z",
  "message": "Perangkat berhasil terhubung dan status aktif."
}
```

Pemeriksaan heartbeat manual:

```bash
curl -X POST http://localhost:8080/api/v1/devices/ping \
  -H "Authorization: Bearer $DEVICE_TOKEN" \
  -d device_id=ESP32-GED-A1
```

Kirimkan bearer `DEVICE_TOKEN` yang valid pada request. Perangkat target harus
sudah terdaftar di MongoDB.

#### 6. Protokol scan HTTP

`POST /api/v1/scans` — `multipart/form-data`, batas total body **16 MiB**.

| Bagian | Nama | Isi |
| --- | --- | --- |
| header | `Authorization` | `Bearer <DEVICE_TOKEN>` |
| header | `X-Device-Timestamp` | waktu kirim RFC3339, selisih ≤ 2 menit dari server |
| header | `X-Device-Signature` | hex HMAC-SHA256 (lihat di bawah), huruf kecil |
| field | `device_id` | harus ada di koleksi `devices` |
| field | `class_id` | **opsional**: kosong = kelas otomatis (kelas mahasiswa yang cocok); bila diisi harus ada di `classes` (dinormalisasi ke huruf besar) |
| field | `captured_at` | waktu capture RFC3339, selisih ≤ 5 menit dari server |
| field | `liveness_passed` | `true` atau `false` |
| file | `image` (1–6 kali) | frame JPEG/PNG berurutan, masing-masing tidak kosong dan ≤ 8 MiB |

Pesan yang ditandatangani = enam baris digabung `\n` (tanpa newline penutup):

```text
<X-Device-Timestamp>
<device_id>
<class_id>            ← huruf besar; baris tetap ada walau kosong (kelas otomatis)
<captured_at>         ← persis seperti dikirim
<liveness_passed>     ← "true" / "false" huruf kecil
<hex SHA-256 dari semua frame digabung berurutan>   ← satu frame = SHA-256 gambar itu
```

Kunci HMAC = `DEVICE_TOKEN`. Server menghitung ulang pesan dari nilai yang
sudah dinormalisasi (spasi di tepi dibuang, `class_id` di-uppercase,
`liveness_passed` di-lowercase). Kirim nilai yang sudah dalam bentuk itu agar
tanda tangan cocok. Menghapus, menambah, atau menukar urutan frame membuat
tanda tangan tidak valid.

Respons `202`:

```json
{
  "status": "Hadir", "result": "hadir", "message": "Hadir",
  "student": { "id": "student-241011001", "nim": "241011001", "name": "Alya Putri" },
  "classId": "IK24-A", "faceVerified": true, "confidence": 0.8123, "reviewReason": "",
  "framesTotal": 4, "framesWithFace": 2, "framesMatched": 2
}
```

`result` ∈ `hadir`, `perlu_ditinjau`, `tidak_terdaftar`,
`wajah_tidak_terdeteksi`; `message` adalah teks pendek untuk layar
(`"Wajah tidak Terdaftar"` untuk `tidak_terdaftar`). `framesWithFace` bisa
lebih kecil dari `framesTotal` karena Go berhenti memproses begitu
`SCAN_MIN_MATCH_FRAMES` frame cocok.

Karena timestamp dicek ±2 menit, perangkat **wajib** menyinkronkan jam lewat
NTP (`configTime(...)` di ESP32) sebelum mengirim scan.

Commissioning scan dilakukan dengan perangkat absensi ESP32 production
sesuai prosedur di §2.5. Scan hanya dicatat bila face service aktif; bila
tidak, backend membalas `502 face_service_error`. Bila penyimpanan foto
(`S3_*`) aktif, frame bukti terbaik disimpan ke Garage dan ditautkan pada
halaman Log.

##### Implementasi di firmware

`postScan()` di `esp32_scanner.ino` mengikuti kontrak ini: SHA-256 bertahap
atas semua frame dan HMAC-SHA256 lewat mbedTLS bawaan core ESP32
(`mbedtls_md_starts/update/finish`, `mbedtls_md_hmac`), `X-Device-Timestamp` =
`captured_at` = waktu NTP setelah frame terakhir diterima, dan body multipart
ditulis bertahap ke `WiFiClient` (frame tidak disalin dua kali di RAM).
Format ini (termasuk perhitungan `Content-Length`) harus sama dengan parser
dan validator backend. Koneksi masih HTTP biasa; untuk server di belakang TLS ganti ke
`WiFiClientSecure`.

#### 7. Catatan keamanan

1. **Satu `DEVICE_TOKEN` untuk semua perangkat.** Bocor satu perangkat berarti
   semua perangkat bisa ditiru. Rencana: secret per perangkat + rotasi.
2. **Heartbeat kini wajib menggunakan `DEVICE_TOKEN` di semua deployment.**
   `GET /api/v1/health` bersifat read-only dan tidak mengubah `last_seen`.
3. **Belum ada nonce anti-replay.** Request scan yang sama bisa diputar ulang
   dalam jendela 2 menit.
4. **"Hadir" dari beberapa frame bukan anti-spoof.** Konsensus ≥ 2 frame
   hanya memastikan wajah yang sama terlihat konsisten selama ±2 detik; foto
   cetak atau layar HP di depan kamera tetap bisa lolos. Untuk mencegah titip
   absen, tambahkan liveness (mis. kedipan/gerak kepala di Python atau sensor
   kedalaman) lalu kirim `liveness_passed=true` hanya bila lolos, dan naikkan
   `SCAN_MIN_MATCH_FRAMES` di atas jumlah frame agar konsensus saja tidak cukup.
5. Firmware memakai HTTP biasa. Token terkirim tanpa enkripsi; di produksi
   gunakan TLS atau jaringan perangkat yang terisolasi.
6. `esp32cam_stream` membuka stream dan `/capture` **tanpa autentikasi** ke
   seluruh jaringan; hanya gunakan saat commissioning pada jaringan terisolasi
   dan jangan hubungkan ke jaringan produksi. `esp32cam_uart` tidak memakai
   Wi-Fi sama sekali.

#### 8. Pemecahan masalah

| Gejala | Kemungkinan penyebab |
| --- | --- |
| Layar "Periksa", kamera GAGAL / `[Cam] tidak merespons` | TX/RX tertukar, GND tidak disatukan, baud berbeda, kamera kurang daya (brownout), atau kartu SD terpasang |
| "Foto rusak (CRC)" / "Transfer foto terputus" | kabel UART terlalu panjang/berisik → turunkan baud di kedua sketch |
| `[CAM] init gagal 0x105` (log kamera) | kabel pita kamera longgar atau board bukan AI Thinker |
| "Jam belum sinkron" | NTP diblokir jaringan kampus; ganti `NTP_SERVER_1` ke server NTP lokal |
| "Server tidak terjangkau" (ESP32) | `SERVER_HOST` salah, Go tidak jalan, atau Windows Firewall memblokir port 8080 |
| Layar tidak menampilkan apa-apa / huruf aneh | `User_Setup.h` TFT_eSPI berbeda atau `LOAD_GFXFF` tidak aktif |
| Wajah terdaftar tetapi "Perlu ditinjau" | wajah cocok di < 2 frame (wajah bergerak/miring, cahaya kurang, jarak terlalu dekat), atau `CLASS_ID` diisi dan mahasiswa bukan anggota kelas itu |
| Wajah terdaftar tetapi "Wajah tidak Terdaftar" | enrollment dengan foto yang jauh berbeda dari kondisi alat (cahaya, sudut kamera). Enroll ulang dengan foto dari kamera alat, atau aktifkan `USE_FLASH`; cek skor di halaman Log |
| "Wajah tidak terdeteksi" padahal ada orang | wajah keluar frame (terlalu dekat < 15 cm), gelap, atau kamera menghadap salah; atur arah kamera dengan `esp32cam_stream` |
| Kamera "tidak merespons" setelah `ON` | catu 5 V kurang (kamera menyedot arus saat init); gunakan adaptor ≥ 2 A |
| ESP32 + layar mati saat 5V kamera disambung ke 5V ESP32 | brownout: USB laptop (±500 mA) tidak kuat untuk ESP32 + TFT + kamera. Beri kamera catu 5V 2A sendiri (GND tetap disatukan); mode Wi-Fi §2.6 hanya untuk commissioning terisolasi |
| Mode Wi-Fi: `[Cam] methics-cam.local tidak ditemukan` | mDNS diblokir jaringan; isi `CAM_HOST` dengan IP kamera dari Serial Monitor kamera |
| Mode Wi-Fi: "Token kamera salah" | `CAM_TOKEN` di `secrets.h` ESP32 utama ≠ `CAM_TOKEN` di `secrets.h` kamera |
| Mode Wi-Fi: compile error `tambahkan #define CAM_TOKEN` | `secrets.h` ESP32 utama belum berisi `CAM_TOKEN` (lihat §2.6 langkah 3) |
| Mode Wi-Fi: kamera restart / `Brownout detector was triggered` (log kamera) | port USB laptop lemah untuk kamera + Wi-Fi; colok MB ke charger HP 5V 2A |
| Serial: `Gagal terhubung ke server. Kode error: -1` (ESP8266) | IP/port salah, server tidak jalan, atau Windows Firewall memblokir port 8080 |
| HTTP `401 invalid_token` | token perangkat ≠ `DEVICE_TOKEN` |
| Dashboard tetap "offline" | interval ping > 2 menit, atau jam server bergeser |
| Scan `401 invalid_signature` | jam perangkat tidak sinkron, `class_id`/`captured_at` berbeda dari yang ditandatangani, atau hash tidak dihitung atas **semua** frame berurutan |
| Scan `400 stale_capture` | `captured_at` lebih dari 5 menit dari jam server |
| Heartbeat/scan `403 unknown_device` | `device_id` belum terdaftar di koleksi `devices` |
| Scan `502 face_service_error` | Face Recognition Service tidak tersedia, versinya tidak sesuai kontrak, atau `FACE_SERVICE_URL` salah; lihat [repository layanan](https://git.algorithmics.web.id/hutao/methics-face) |
| Kolom Foto di Log berisi `-` | penyimpanan foto nonaktif (`S3_*` kosong), Garage tidak terjangkau (lihat log `image storage: …`), scan dibuat sebelum `S3_*` diisi, atau foto sudah lewat masa retensi |


## Integrasi Face Recognition Service

### Integrasi Face Recognition Service dengan Go

> Dokumen ini menjelaskan **kontrak integrasi dari sisi Go** dan penyimpanan
> foto/template. Source Python dan instruksi build/deployment layanan telah
> dipindahkan ke [repository hutao/methics-face](https://git.algorithmics.web.id/hutao/methics-face).
> Jika detail implementasi layanan berbeda dari dokumen ini, verifikasi
> kontrak pada repository layanan dan klien Go sebelum deployment.

#### 1. Pembagian tugas: satu API publik

```text
Browser / ESP32 ──► Go (satu-satunya API publik, :8080)
                     │  POST /register?student_id=…  (satu file per request, paralel) — enrollment
                     │  POST /embed                   (1 file "image")     — scan
                     ├──────────────────────────────► Face Recognition Service (internal)
                     │◄──────────── embedding ArcFace 512-d (+ confidence) per foto
                     │
                     ├── simpan semua embedding pose → MongoDB face_templates   (enrollment)
                     └── cosine similarity 1:N       → bandingkan dengan face_templates (scan)
```

Face service bersifat **stateless**: ia tidak menyimpan apa pun dan tidak
terhubung ke MongoDB (`student_id` pada `/register` hanya dikembalikan
lagi). Go yang:

- menyimpan template wajah (1–6 embedding per mahasiswa) di koleksi `face_templates`,
- melakukan pencocokan 1:N (cosine similarity) saat scan,
- menentukan status `Hadir` / `Perlu ditinjau` / `Ditolak`.

Akibatnya perangkat ESP32 dan dashboard cukup berbicara dengan **satu** API
(Go). Face service tidak perlu dan tidak boleh diekspos ke jaringan publik.

Klien Go ada di `internal/api/face.go` (klien `/embed` & `/register`, validasi
enrollment, pencocokan), dipanggil dari `EnrollFace` dan `receiveScan` di
`internal/api/server.go`. Foto wajah opsional disimpan di Garage (S3), lihat §8.

#### 2. Endpoint Python yang dipakai Go

| Endpoint | Dipakai untuk | Request dari Go | Respons yang dibaca Go |
| --- | --- | --- | --- |
| `GET /health` | status di dashboard & `/api/v1/health` | — | `model_loaded`, `det_model`, `rec_model` |
| `POST /embed` | scan perangkat | multipart, 1 file `image` | `embedding` (512-d), `confidence`, `processing_time_ms` |
| `POST /register?student_id=<id>` | enrollment | multipart; Go mengirim satu file `images` per request secara paralel; batas batch dan konkurensi mengikuti implementasi pada repository layanan | `num_templates`, `templates[].embedding`, `templates[].confidence`, `processing_time_ms` |
| `GET /models` | katalog model (via Go) | query opsional `refresh_remote` | `repo_id`, `defaults`, status online, katalog per kategori |
| `POST /models/download` | mengunduh model (via Go) | JSON `model_id`, opsional `force` | status, `model_id`, path, pesan |
| `POST /models/switch` | mengganti model aktif (via Go) | JSON opsional `det_model`, `rec_model`, `conf_threshold`, `iou_threshold` | status dan model aktif |

Layanan eksternal harus melindungi endpoint internal dengan bearer
`FACE_SERVICE_TOKEN`; Go mengirim token pada setiap request, termasuk health
check. Kebijakan autentikasi, dokumentasi API, dan konfigurasi CORS dijelaskan di
[repository layanan](https://git.algorithmics.web.id/hutao/methics-face).
Python memilih **wajah dengan confidence deteksi tertinggi** pada tiap foto;
foto berisi beberapa orang tidak ditolak, jadi pastikan
foto enrollment hanya berisi mahasiswa yang bersangkutan. `return_aligned`
tidak dipakai (crop wajah tidak dikirim balik). Endpoint manajemen model
dapat diakses melalui Go pada `/api/v1/face/models`,
`/api/v1/face/models/download`, dan `/api/v1/face/models/switch`; ketiganya
khusus administrator dan meneruskan format JSON layanan Python.

Status HTTP dari Python:

| Status Python | Perlakuan Go |
| --- | --- |
| `200` | diproses |
| `400` (gambar kosong/tidak bisa di-decode, atau lebih dari 6 foto) | enrollment `400 invalid_image`; scan `400 invalid_image` |
| `422` dengan `detail` "No face detected …" | enrollment `422 no_face` (nomor foto dibaca dari `image #N`); scan dicatat `Ditolak` "Wajah tidak terdeteksi" |
| `422` validasi FastAPI, `503` model belum siap, lainnya, timeout, tidak terjangkau | `502 face_service_error`; scan **tidak** dicatat |

#### 3. Enrollment (administrator)

`POST /partials/mahasiswa/{nim}/face-template` (tombol **Enroll wajah** /
**Enroll ulang** di halaman Mahasiswa, pilih 1–6 file sekaligus) atau
`POST /api/v1/students/{nim}/face-template` (JSON, field `images`):

1. Go membaca 1–6 foto (maks 8 MB per foto; field `images`, atau `image`
   untuk klien lama), lalu mengirim setiap foto ke `POST /register` sebagai
   request terpisah secara paralel. Python menjalankan maksimal dua inferensi
   sekaligus di worker thread agar event loop API tetap responsif; hasil
   digabung Go menurut urutan foto awal. Timeout enrollment disesuaikan
   dengan jumlah batch inferensi.
   Pose yang disarankan: depan, kiri (~30°), kanan (~30°), atas, bawah,
   senyum. Satu foto depan tetap boleh.
2. Foto tanpa wajah → `422 no_face` dengan nomor fotonya.
3. **Konsistensi:** setiap foto harus orang yang sama: kemiripannya dengan
   foto pertama **atau** dengan rata-rata foto lain harus ≥
   `FACE_POSE_THRESHOLD` (default 0.30, lebih longgar dari ambang scan karena
   pose miring wajar kurang mirip foto depan). Bila tidak →
   `422 inconsistent_faces` (mis. foto ke-4 orang lain).
4. **Duplikat:** setiap embedding dibandingkan dengan template mahasiswa
   **lain**. Bila kemiripan ≥ ambang, enrollment ditolak
   (`409 face_already_enrolled`, menyebut NIM pemilik wajah) untuk mencegah
   satu wajah terdaftar di dua NIM (titip absen). Template milik mahasiswa itu
   sendiri diabaikan sehingga enrollment ulang tidak bentrok.
5. Semua embedding di-*upsert* ke **satu** dokumen `face_templates` milik
   mahasiswa itu (field `encodings`); enrollment ulang mengganti seluruh pose
   lama.

Dokumen `face_templates`:

```json
{
  "student_id": "student-241011001",
  "encodings": [[0.0123, -0.0456, "… 512 angka …"], "… 1–6 vektor …"],
  "template_count": 6,
  "dimension": 512,
  "detection_confidences": [0.93, 0.91, 0.9, 0.88, 0.9, 0.92],
  "image_key": "enroll/student-241011001/20261028T080000Z-3f9a1c2b4d5e.jpg",
  "image_keys": ["enroll/student-241011001/…", "… satu per foto …"],
  "created_at": "2026-10-28T08:00:00Z",
  "updated_at": "2026-10-28T08:00:00Z"
}
```

Dokumen dari versi lama (satu foto, field `encoding` dan
`detection_confidence`) tetap dibaca dan dicocokkan sebagai satu pose;
enrollment ulang mengubahnya ke format baru.

#### 4. Verifikasi saat scan (perangkat)

Perangkat menyalakan kamera saat sensor ultrasonik mendeteksi orang, lalu
mengirim 1–6 frame dari jendela scan ±2 detik dalam satu
`POST /api/v1/scans` (setelah token, HMAC, timestamp, dan perangkat lolos
validasi; kode di `internal/api/scan.go`):

1. `matchFrames` mengirim frame **satu per satu** ke `POST /embed`. Frame
   tanpa wajah (`422`) atau rusak (`400`) dilewati; face service mati /
   timeout → `502`, scan tidak dicatat.
2. Embedding tiap frame dibandingkan dengan **semua pose semua** mahasiswa
   (1:N). Skor seorang mahasiswa = skor pose terbaiknya. Frame dengan skor ≥
   `FACE_SIM_THRESHOLD` dihitung sebagai satu "suara" untuk mahasiswa itu.
   Begitu satu mahasiswa mendapat `SCAN_MIN_MATCH_FRAMES` (2) suara, sisa
   frame tidak diproses (lebih cepat untuk wajah terdaftar).
3. `decideFrames` memilih mahasiswa dengan suara terbanyak (seri → skor
   tertinggi):
   - ≥ `SCAN_MIN_MATCH_FRAMES` suara (atau `liveness_passed=true`) →
     **Hadir** di kelas mahasiswa itu (`class_id` kosong) atau di kelas
     perangkat bila mahasiswa anggotanya;
   - 1 suara saja → **Perlu ditinjau**;
   - ada wajah tetapi tidak ada suara → **Ditolak**, "Wajah tidak
     Terdaftar" (`result: tidak_terdaftar`);
   - tidak ada wajah di semua frame → **Ditolak**, "Wajah tidak terdeteksi".
4. `confidence` pada event = skor terbaik mahasiswa yang menang (atau skor
   tertinggi keseluruhan untuk wajah tidak terdaftar).

Beberapa pose meningkatkan peluang cocok saat mahasiswa tidak tepat
menghadap kamera, tetapi juga sedikit menaikkan peluang salah cocok; ukur
ulang ambang setelah data nyata terkumpul (§7). Konsensus beberapa frame
hanya memastikan wajah yang sama terlihat konsisten selama ±2 detik —
**bukan** anti-spoof.

#### 5. Konfigurasi

Sisi Go (`.env`):

| Variabel | Default | Keterangan |
| --- | --- | --- |
| `FACE_SERVICE_URL` | `http://127.0.0.1:8000` | base URL Face Recognition Service internal, diatur mengikuti topologi deployment |
| `FACE_SERVICE_TOKEN` | — | wajib minimal 32 karakter; harus sama dengan token Face Recognition Service |
| `FACE_SIM_THRESHOLD` | `0.45` | ambang cosine similarity untuk scan dan cek duplikat; samakan dengan `SIM_THRESHOLD` di Python |
| `FACE_POSE_THRESHOLD` | `0.30` | ambang kemiripan antar-foto dalam satu enrollment (cek konsistensi pose) |
| `SCAN_MIN_MATCH_FRAMES` | `2` | jumlah frame scan yang harus cocok dengan mahasiswa yang sama agar langsung **Hadir** tanpa liveness dari perangkat |
| `FACE_SERVICE_TIMEOUT_SECONDS` | `15` | batas waktu `/embed`; enrollment mendapat nilai ini dikali jumlah gelombang (dua inferensi Python bersamaan; 6 foto → 45 detik). Health memakai 3 detik |

Request enrollment juga mendapat batas baca/tulis sendiri **3 menit**
(`api.EnrollRequestTimeout`), menggantikan `ReadTimeout`/`WriteTimeout`
server 15 detik. Foto enrollment dikirim ke service secara paralel sesuai
konfigurasi layanan; unggahan foto ke Garage juga paralel. Detail log service
dan batas konkurensi mengikuti dokumentasi repository methics-face. Go
mencatat hasil batch `enrollment … berhasil/gagal (N foto, durasi)`. Karena
penyimpanan template di Go dilakukan setelah semua response foto lolos
validasi, batch gagal tidak menulis sebagian embedding.

Konfigurasi Python, pilihan model, host, port, dan worker diatur pada
[repository Face Recognition Service](https://git.algorithmics.web.id/hutao/methics-face);
dokumen ini tidak mendefinisikan lagi path source atau perintah peluncur Python.

#### 6. Deployment production

Deploy Face Recognition Service dan aplikasi Go secara terpisah sesuai
dokumentasi production pada
[repository methics-face](https://git.algorithmics.web.id/hutao/methics-face)
dan [panduan utama](#mulai-cepat). `FACE_SERVICE_TOKEN` minimal 32
karakter dan harus sama pada kedua service. Atur `FACE_SERVICE_URL` di Go agar
mengarah ke alamat privat Face Recognition Service.

```powershell
go build -trimpath -ldflags="-s -w" -o bin/methics.exe ./cmd/server
.\bin\methics.exe
```

Pastikan service hanya dapat dijangkau melalui jaringan internal dan batasi
firewall agar hanya host Go yang dapat mengaksesnya. Jangan memublikasikan
port service ke internet.

Cek integrasi:

```bash
curl http://localhost:8080/api/v1/health
```

Lakukan enrollment melalui halaman **Mahasiswa** dan commissioning scan
menggunakan perangkat absensi production (`bagian Perangkat` §2).

Proses pemuatan model, akses Hugging Face, dan persistensi cache mengikuti
instruksi deployment di repository Face Recognition Service.

#### 7. Keamanan dan hal yang perlu diperhatikan

- Face Recognition Service harus memverifikasi bearer `FACE_SERVICE_TOKEN`
  sesuai konfigurasi pada repository layanan.
- Port service harus tetap di loopback atau jaringan privat; bearer token
  bukan pengganti isolasi jaringan.
- **Ambang `0.45` belum dikalibrasi** dengan data kampus. Ukur FAR/FRR
  dengan beberapa foto per mahasiswa sebelum dipakai sungguhan.
- Pencocokan memuat seluruh template tiap scan (O(N)); cukup untuk ratusan
  hingga beberapa ribu mahasiswa. Untuk skala lebih besar, batasi kandidat
  per kelas atau tambahkan cache.
- Mengganti model recognition (`/models/switch` atau `REC_MODEL_PATH`)
  membuat embedding lama tidak sebanding → lakukan enrollment ulang. Template
  berdimensi berbeda otomatis diabaikan, tetapi model berbeda dengan dimensi
  sama **tidak** terdeteksi.
- Kapasitas inferensi, jumlah worker, dan konkurensi merupakan tanggung jawab
  Face Recognition Service; gunakan panduan beban/performa di repository
  layanan dan uji bersama konfigurasi deployment aktual.
- Liveness/anti-spoof belum ada di Python maupun Go; `liveness_passed` masih
  klaim perangkat.
- Python tidak menyimpan foto. Go menyimpan foto di Garage hanya bila
  `S3_*` diisi (lihat §8). Embedding maupun foto adalah data biometrik
  sensitif.

#### 8. Penyimpanan foto (S3 Garage)

Pencocokan hanya memakai embedding, jadi foto tidak dibutuhkan untuk
absensi. Foto tetap disimpan untuk tiga keperluan:

1. **Enrollment ulang otomatis** saat model recognition diganti (embedding
   model lama tidak sebanding dengan model baru).
2. **Peninjauan** scan `Perlu ditinjau` oleh dosen/admin.
3. **Audit dan sanggahan** (mahasiswa protes, dugaan titip absen).

Alur:

| Kejadian | Kunci objek | Dicatat di | Masa simpan |
| --- | --- | --- | --- |
| Enrollment berhasil | `enroll/{student_id}/{waktu}-{acak}.jpg`, satu per foto | `face_templates.image_keys` (semua) dan `image_key` (foto pertama) | sampai enrollment ulang (foto lama dihapus) |
| Scan `Hadir` | `scans/{yyyy}/{mm}/{dd}/{waktu}-{device}-{acak}.jpg` | `scan_events.image_key` | `IMAGE_RETENTION_DAYS` (30) |
| Scan `Perlu ditinjau` / `Ditolak` | sama | sama | `IMAGE_REVIEW_RETENTION_DAYS` (90) |
| Request gagal validasi / face service error | — | — | tidak disimpan |

- Unggah diberi batas 3 detik. Bila Garage mati, absensi dan enrollment
  tetap berjalan tanpa foto; kesalahannya dicatat di log.
- Job retensi berjalan saat start lalu tiap jam: foto kedaluwarsa dihapus
  dari bucket dan `image_key` di-`$unset`; event absensinya tetap ada.
- Foto dilihat lewat `GET /api/v1/images/{key}` (sesi staf; dosen hanya
  kelasnya). Bucket tidak perlu dan tidak boleh dibuka publik.
- Klien S3 ada di `internal/storage/s3.go` (pustaka standar, AWS Signature
  V4, gaya path); logika foto di `internal/api/images.go`.
- `GET /api/v1/health` menampilkan `imageStorage.enabled/online`; log start
  menulis `image storage: bucket "…" siap` atau peringatan.

Menyiapkan Garage (di server):

```bash
garage bucket create methics-faces
garage key create methics-app            # catat Key ID dan Secret key
garage bucket allow --read --write methics-faces --key methics-app
```

Lalu isi `S3_ENDPOINT=http://10.100.1.11:3900`, `S3_REGION=garage` (sama
dengan `s3_region` di `garage.toml`), `S3_BUCKET=methics-faces`,
`S3_ACCESS_KEY_ID`, dan `S3_SECRET_ACCESS_KEY` di `.env`. Jangan memakai
`admin_token` atau akun root server untuk aplikasi.

Menghapus mahasiswa (halaman Mahasiswa atau `DELETE /api/v1/students/{nim}`)
ikut menghapus foto enrollment dan foto scannya dari Garage.

Foto enrollment dibuka lewat tombol **Lihat foto** (foto pertama) dan
nomor **2–6** di sebelahnya (pose lain) di halaman Mahasiswa. Tombolnya
muncul hanya bila foto tersimpan di Garage; enrollment yang dibuat sebelum
`S3_*` diisi tidak punya foto. Foto scan dibuka dari kolom **Foto**
di halaman Log. Belum ada: enrollment ulang otomatis dari foto tersimpan.

#### 9. Apa yang disimpan dari hasil Python

| Saat | Embedding dari Python | Yang disimpan |
| --- | --- | --- |
| Enrollment (`/register`) | disimpan Go | `face_templates.encodings` (1–6 × 512 angka) + `template_count`, `dimension`, `detection_confidences`, `image_key(s)`, waktu |
| Scan perangkat (`/embed`, 1–6 frame) | **tidak** disimpan; hanya dipakai untuk pencocokan lalu dibuang | `scan_events`: `student_id` kandidat, `confidence` (cosine similarity), `status`, `review_reason`, `frames_total/with_face/matched`, `image_key` (satu frame bukti) |

Face Recognition Service tidak menjadi sumber data template enrollment;
folder/cache model yang dikelola service bukan database enrollment. Template
embedding tetap disimpan oleh Go di MongoDB.


## Spesifikasi Kebutuhan

### SRS — Algorithmics Face Recognation

> **Status dokumen: KERANGKA SIAP-ISI.** Kebutuhan di bawah ini diangkat dari
> kode yang ada saat ini, lalu ditandai status implementasinya. Bagian yang
> hanya kamu yang tahu (target angka, regulasi, jumlah perangkat, jadwal)
> ditandai **`ISI:`** untuk dilengkapi.
>
> Penanda status: `[TERSEDIA]` sudah ada di kode · `[SEBAGIAN]` sebagian ·
> `[BELUM]` belum ada · `TBD` perlu keputusan.
>
> Aplikasi hanya menerima konfigurasi `APP_ENV=production`. Status tersebut
> tidak menyatakan bahwa deployment sudah tersertifikasi atau siap beroperasi:
> commissioning perangkat, TLS, kebijakan biometrik, backup, dan kebutuhan
> institusi tetap harus dipenuhi.

#### 0. Kendali Dokumen

| Item | Nilai |
| --- | --- |
| Nama produk | Algorithmics Face Recognation — sistem absensi mahasiswa ITBJH |
| Versi dokumen | ISI: 0.1 (draf) |
| Tanggal | ISI: |
| Penyusun | ISI: |
| Peninjau | ISI: |
| Status | ISI: Draf / Untuk ditinjau / Disetujui |

Riwayat revisi:

| Versi | Tanggal | Perubahan | Oleh |
| --- | --- | --- | --- |
| 0.8 | 2026-11-04 | Dosen dan administrator dapat menambah/menghapus kelas; filter kelas memakai data database; penghapusan kelas yang masih direferensikan ditolak | ISI: |
| 0.7 | 2026-10-28 | Konfigurasi production-only; token face service dan heartbeat wajib; health read-only; dokumentasi diselaraskan; suite test otomatis dihapus dan status pemeliharaan diperbarui | ISI: |
| 0.1 | ISI: | Kerangka awal dari kode yang ada | ISI: |
| 0.6 | 2026-10-26 | Hapus mahasiswa (FR-STU-10), buat & hapus akun dosen (FR-AUTH-09, FR-AUTHZ-06 sebagian); matriks dihitung ulang | ISI: |
| 0.5 | 2026-10-26 | Firmware perangkat ESP32 + ESP32-CAM (UART); IF-HW-01/04 diperbarui, tambah IF-HW-05 | ISI: |
| 0.4 | 2026-10-26 | Penyimpanan foto wajah di object storage S3 (Garage) dengan retensi; tambah FR-BIO-11, IF-SW-06, NFR-REL-07; bug cakupan log (FR-LOG-01) diperbaiki; status privasi & test diperbarui; matriks dihitung ulang | ISI: |
| 0.3 | 2026-10-26 | Face service Python (SCRFD + ArcFace) terhubung ke Go; penyimpanan & pencocokan embedding di Go; status biometrik diperbarui; tambah FR-BIO-10 | ISI: |
| 0.2 | 2026-10-21 | Selaras dengan MongoDB; tambah log pemindaian (FR-LOG), heartbeat perangkat (FR-DEV-16); status biometrik diturunkan ke `[SEBAGIAN]` karena face service belum ada di repo; matriks dihitung ulang | ISI: |

#### 1. Pendahuluan

##### 1.1 Tujuan

Menetapkan kebutuhan perangkat lunak untuk sistem absensi mahasiswa berbasis
pengenalan wajah: halaman web untuk staf (administrator/dosen) dan API untuk
perangkat kamera (ESP32-CAM) beserta layanan verifikasi wajah.

##### 1.2 Ruang lingkup

Cakupan produk:

- Dashboard web untuk staf akademik (absensi, kelas, mahasiswa, laporan, akses).
- API untuk perangkat ESP32 yang mengirim capture wajah.
- Layanan verifikasi wajah internal (embedding, pencocokan).
- Penyimpanan data akademik dan event absensi.

Di luar cakupan:

- Aplikasi mobile.
- Akun/login untuk mahasiswa.
- Perangkat keras kamera itu sendiri (hanya kontrak integrasinya).
- `ISI:` integrasi SIAKAD / sistem akademik kampus, bila direncanakan.

##### 1.3 Definisi & singkatan

| Istilah | Arti |
| --- | --- |
| Staf | Pengguna ber-akun: `administrator` atau `dosen` |
| Mahasiswa | Subjek absensi; record akademik, **bukan** akun aplikasi |
| Enrollment | Pendaftaran template wajah seorang mahasiswa (1–6 pose) |
| Embedding | Vektor numerik fitur wajah (512 dimensi pada implementasi ini, ArcFace R50) |
| Template wajah | Embedding tersimpan untuk seorang mahasiswa (`encodings` 1–6 pose) |
| Scan | Satu pengiriman capture multi-frame dari perangkat dan keputusannya |
| Liveness | Bukti bahwa wajah berasal dari orang hidup, bukan foto |
| HMAC | Kode autentikasi pesan berbasis hash untuk menandatangani request |
| SRS | Software Requirements Specification |

##### 1.4 Referensi

- `panduan utama` — cara menjalankan & konfigurasi.
- `bagian Kontrak API` — kontrak API & skema HMAC perangkat.
- `bagian Arsitektur` — arsitektur & keputusan.
- `bagian Panduan Kode` — penjelasan kode.
- `panduan operasional` — status & risiko.
- `bagian Database` — skema MongoDB.
- `bagian Perangkat` — firmware & protokol perangkat.
- `bagian Integrasi Face Service` — kontrak layanan wajah.
- `ISI:` standar/regulasi yang berlaku (mis. UU PDP), kebijakan kampus, dokumen akademik.

##### 1.5 Ikhtisar dokumen

Bagian 2 menjelaskan konteks produk; Bagian 3 kebutuhan fungsional; Bagian 4
antarmuka eksternal; Bagian 5 kebutuhan non-fungsional; Bagian 6 batasan;
Bagian 7 matriks keterlacakan; Bagian 8 daftar hal yang harus diputuskan.

#### 2. Deskripsi Umum

##### 2.1 Perspektif produk

Sistem baru yang menggantikan pencatatan absensi manual. Terdiri dari:

```text
Perangkat ESP32-CAM ──► Backend Go ──► Layanan verifikasi wajah (Python, stateless)
Perangkat ESP8266 ───►     │
(heartbeat)                ├──► Basis data (MongoDB): template, absensi
                           └──► Object storage S3 (Garage): foto wajah (opsional)
Staf (browser) ──────► Backend Go (halaman + API)
```

Backend Go adalah satu-satunya pintu masuk publik; layanan wajah tidak
diekspos ke jaringan luar.

##### 2.2 Fungsi utama

1. Autentikasi & otorisasi staf berbasis role.
2. Ringkasan absensi harian.
3. Pengelolaan data mahasiswa dan enrollment wajah.
4. Pencatatan absensi otomatis dari perangkat.
5. Pelaporan & riwayat scan.
6. Integrasi perangkat (ESP32) dengan autentikasi & tanda tangan request.

##### 2.3 Aktor & karakteristik

| Aktor | Deskripsi | Hak akses |
| --- | --- | --- |
| Administrator | Staf pengelola sistem/roster | Semua kelas; kelola mahasiswa; enrollment; halaman akses; tambah/hapus kelas |
| Dosen | Pengajar | Baca kelas yang ditugaskan; tambah kelas (otomatis ditugaskan kepadanya); hapus kelas dalam cakupannya jika tidak dipakai |
| Perangkat (ESP32/ESP8266) | Klien mesin | Endpoint scan (token + HMAC), heartbeat ping, dan health |
| Mahasiswa | Subjek absensi | Tidak punya akses aplikasi |
| `ISI:` Operator/IT | ISI: bila ada | ISI: |

##### 2.4 Lingkungan operasi

- **Server:** `ISI:` OS & spesifikasi produksi. Aplikasi hanya menerima konfigurasi production. Zona waktu server menentukan batas "hari ini".
- **Basis data:** MongoDB; `MONGODB_URI` wajib dikonfigurasi, database `attendance` secara default. `ISI:` versi & topologi produksi (single node / replica set).
- **Perangkat:** ESP32 utama (HC-SR04, TFT, buzzer, Wi-Fi) + ESP32-CAM via UART untuk scan multi-frame dan heartbeat (commissioning fisik belum dinyatakan selesai); firmware ESP32-CAM Wi-Fi dan ESP8266 hanya alat commissioning. `ISI:` jumlah perangkat & lokasi.
- **Jaringan:** `ISI:` LAN kampus / VPN / TLS reverse proxy.
- **Object storage:** Garage (S3-compatible), mis. `http://10.100.1.11:3900`, bucket `methics-faces`; opsional.
- **Layanan wajah:** Python + FastAPI + ONNX Runtime (SCRFD, ArcFace R50); source dan deployment ada di [repository methics-face](https://git.algorithmics.web.id/hutao/methics-face), terintegrasi melalui endpoint internal bertoken `FACE_SERVICE_TOKEN`.

##### 2.5 Batasan desain

| ID | Batasan |
| --- | --- |
| CO-01 | Frontend server-rendered (Go `html/template` + htmx); bukan SPA. |
| CO-02 | CSP `script-src 'self'`; tidak ada script inline maupun CDN skrip. |
| CO-03 | Aset skrip pihak ketiga di-vendor lokal dan versi-terpin. |
| CO-04 | Backend tidak melakukan inference wajah; itu tugas layanan Python. |
| CO-05 | Browser tidak pernah menerima template wajah; foto hanya dikirim ke staf berwenang lewat endpoint ber-otorisasi. |
| CO-06 | Autentikasi staf memakai session cookie; perangkat memakai token + HMAC. |

##### 2.6 Asumsi & ketergantungan

| ID | Asumsi/Ketergantungan |
| --- | --- |
| AS-01 | Jam perangkat ESP32 tersinkron dengan server (toleransi ±2 menit untuk request, ±5 menit untuk capture). |
| AS-02 | Layanan wajah berjalan dan dapat dijangkau backend melalui jaringan internal. |
| AS-03 | `ISI:` ketersediaan jaringan di lokasi perangkat. |
| AS-04 | `ISI:` data mahasiswa (NIM, nama, kelas) berasal dari sumber tepercaya. |
| AS-05 | Object storage Garage dapat dijangkau backend bila penyimpanan foto diaktifkan; bila tidak, absensi tetap berjalan tanpa foto. |

#### 3. Kebutuhan Fungsional

Kolom **Pri** = prioritas (W wajib, S sebaiknya, B bila memungkinkan).

##### 3.1 Autentikasi & sesi

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-AUTH-01 | Staf dapat login dengan username & password | W | `[TERSEDIA]` |
| FR-AUTH-02 | Password disimpan ter-hash (bcrypt), tidak pernah plaintext | W | `[TERSEDIA]` |
| FR-AUTH-03 | Percobaan login dibatasi per IP (10 kali / 5 menit) | W | `[TERSEDIA]` |
| FR-AUTH-04 | Sesi diperbarui (regenerasi token) setelah login berhasil | W | `[TERSEDIA]` |
| FR-AUTH-05 | Staf dapat logout dan sesinya diakhiri | W | `[TERSEDIA]` |
| FR-AUTH-06 | Sesi kedaluwarsa → pengguna diarahkan ke login, tanpa kehilangan konteks halaman | S | `[TERSEDIA]` |
| FR-AUTH-07 | Sesi punya batas umur dan batas idle | W | `[TERSEDIA]` (8 jam / idle 30 menit) |
| FR-AUTH-08 | Semua aksi tulis staf dilindungi proteksi CSRF | W | `[TERSEDIA]` |
| FR-AUTH-09 | Administrator dapat mengelola akun staf (buat, ubah, nonaktifkan, reset password) | W | `[SEBAGIAN]` (buat & hapus akun dosen di `/akses`; ubah, nonaktifkan, reset password belum) |
| FR-AUTH-10 | Akun dapat dikunci setelah kegagalan berulang pada akun yang sama | S | `[BELUM]` |
| FR-AUTH-11 | `ISI:` kebijakan password (panjang, kompleksitas, masa berlaku) | — | TBD |

##### 3.2 Otorisasi

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-AUTHZ-01 | Administrator dapat mengakses semua kelas | W | `[TERSEDIA]` |
| FR-AUTHZ-02 | Dosen hanya melihat data kelas yang ditugaskan padanya | W | `[TERSEDIA]` |
| FR-AUTHZ-03 | Data dibatasi di sisi server (query), bukan di klien | W | `[TERSEDIA]` |
| FR-AUTHZ-04 | Pendaftaran mahasiswa & enrollment wajah hanya administrator | W | `[TERSEDIA]` |
| FR-AUTHZ-05 | Halaman manajemen akses hanya administrator | W | `[TERSEDIA]` |
| FR-AUTHZ-06 | Administrator dapat mengatur penugasan kelas ke dosen melalui UI | S | `[SEBAGIAN]` (kelas dipilih saat membuat akun; belum bisa diubah) |
| FR-AUTHZ-07 | Aksi pengguna tercatat pada jejak audit | S | `[BELUM]` |

##### 3.3 Dashboard

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-DASH-01 | Menampilkan ringkasan: jumlah mahasiswa, kelas, scan terverifikasi, perlu ditinjau, template wajah | W | `[TERSEDIA]` |
| FR-DASH-02 | Menampilkan kehadiran per kelas (hadir/total, persentase) hari ini | W | `[TERSEDIA]` |
| FR-DASH-03 | Menampilkan status perangkat terakhir (online bila < 2 menit) | S | `[TERSEDIA]` |
| FR-DASH-04 | Menampilkan status layanan verifikasi wajah | S | `[TERSEDIA]` |
| FR-DASH-05 | Menampilkan riwayat scan terbaru | S | `[TERSEDIA]` (8 baris) |
| FR-DASH-06 | Memperbarui data secara berkala tanpa reload penuh | S | `[TERSEDIA]` (15 detik) |
| FR-DASH-07 | Menampilkan tren/grafik historis | B | `[BELUM]` |

##### 3.4 Kelas

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-CLASS-01 | Menampilkan ringkasan per kelas (jumlah mahasiswa, ter-enroll, hadir) | W | `[TERSEDIA]` |
| FR-CLASS-02 | Administrator dan dosen dapat membuat/menghapus kelas; penghapusan ditolak selama masih direferensikan | W | `[SEBAGIAN]` (tambah/hapus tersedia; ubah belum tersedia) |
| FR-CLASS-03 | Kelas memiliki kode unik dan nama | W | `[TERSEDIA]` |

##### 3.5 Mahasiswa & enrollment

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-STU-01 | Menampilkan roster mahasiswa beserta kelas dan status wajah | W | `[TERSEDIA]` |
| FR-STU-02 | Mencari mahasiswa berdasarkan nama atau NIM | W | `[TERSEDIA]` |
| FR-STU-03 | Menyaring roster per kelas | S | `[TERSEDIA]` |
| FR-STU-04 | Administrator dapat menambah mahasiswa (nama, NIM, kelas) | W | `[TERSEDIA]` |
| FR-STU-05 | Kesalahan validasi ditampilkan pada form (inline), bukan halaman error | W | `[TERSEDIA]` |
| FR-STU-06 | NIM unik; penambahan dengan NIM ganda ditolak dengan pesan jelas | W | `[TERSEDIA]` |
| FR-STU-07 | Administrator dapat melakukan enrollment wajah mahasiswa | W | `[TERSEDIA]` (1–6 foto pose berbeda; validasi konsistensi pose & anti-duplikasi) |
| FR-STU-08 | Enrollment gagal (wajah tidak terdeteksi/service mati/pose inkonsisten) memberi pesan yang dapat ditindaklanjuti per foto | W | `[TERSEDIA]` |
| FR-STU-09 | Administrator dapat mengubah data mahasiswa | S | `[BELUM]` |
| FR-STU-10 | Administrator dapat menghapus/menonaktifkan mahasiswa | S | `[TERSEDIA]` (hapus beserta template wajah, foto, dan riwayat scan; nonaktifkan belum) |
| FR-STU-11 | Mahasiswa dapat diimpor secara massal (unggah CSV) | B | `[BELUM]` |
| FR-STU-12 | Riwayat absensi per mahasiswa | S | `[TERSEDIA]` (via laporan) |

##### 3.6 Laporan

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-REP-01 | Rekap scan hari ini: terverifikasi, perlu ditinjau, ditolak | W | `[TERSEDIA]` |
| FR-REP-02 | Komposisi status (jumlah & persentase) | S | `[TERSEDIA]` |
| FR-REP-03 | Kehadiran per kelas (persentase) | W | `[TERSEDIA]` |
| FR-REP-04 | Riwayat scan terbaru (maks 50) dengan waktu, mahasiswa, kelas, perangkat, status | W | `[TERSEDIA]` |
| FR-REP-05 | Laporan dapat dicetak | S | `[TERSEDIA]` |
| FR-REP-06 | Filter rentang tanggal | W | `[BELUM]` |
| FR-REP-07 | Ekspor CSV/Excel | S | `[BELUM]` |
| FR-REP-08 | Rekap per mahasiswa (jumlah hadir/alpha dalam periode) | W | `[BELUM]` |
| FR-REP-09 | `ISI:` format/jadwal laporan resmi untuk akademik | — | TBD |

##### 3.7 Perangkat & absensi otomatis

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-DEV-01 | Perangkat mengautentikasi dengan token + tanda tangan HMAC + timestamp | W | `[TERSEDIA]` |
| FR-DEV-02 | Request dengan timestamp kedaluwarsa ditolak | W | `[TERSEDIA]` |
| FR-DEV-03 | Hanya perangkat terdaftar yang diterima | W | `[TERSEDIA]` (scan dan heartbeat memeriksa perangkat; health read-only) |
| FR-DEV-04 | Kelas pada scan harus valid | W | `[TERSEDIA]` |
| FR-DEV-05 | Capture diverifikasi ke template wajah melalui layanan wajah | W | `[TERSEDIA]` |
| FR-DEV-06 | Frame scan divalidasi (1–6 frame, per frame ≤ 8 MB, total ≤ 16 MB; frame bukan gambar/rusak ditolak/dilewati) | W | `[TERSEDIA]` |
| FR-DEV-07 | Baris absensi dicatat dengan status dan alasan peninjauan | W | `[TERSEDIA]` |
| FR-DEV-08 | Status ditentukan berlapis: konsensus ≥ 2 frame cocok (`SCAN_MIN_MATCH_FRAMES`) atau liveness → Hadir; selain itu Perlu ditinjau / Ditolak | W | `[TERSEDIA]` |
| FR-DEV-09 | Waktu terakhir perangkat aktif diperbarui | S | `[TERSEDIA]` |
| FR-DEV-10 | Endpoint health publik untuk diagnostik | S | `[TERSEDIA]` |
| FR-DEV-11 | Secret unik per perangkat + rotasi/revokasi | W | `[BELUM]` |
| FR-DEV-12 | Proteksi replay (nonce sekali pakai) | W | `[BELUM]` |
| FR-DEV-13 | Deteksi liveness yang tervalidasi (anti-spoof) | W | `[BELUM]` |
| FR-DEV-14 | Administrator dapat mengelola daftar perangkat | S | `[BELUM]` |
| FR-DEV-15 | Perangkat menerima konfigurasi/pembaruan dari server | B | `[BELUM]` |
| FR-DEV-16 | Perangkat mengirim heartbeat berkala sehingga status "Terhubung" tampil di dashboard | S | `[TERSEDIA]` (`/api/v1/devices/ping` + firmware ESP32 `esp32_scanner`/ESP8266) |

##### 3.8 Pengenalan wajah

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-BIO-01 | Enrollment memerlukan tepat satu wajah terdeteksi pada tiap foto (1–6 pose) | W | `[TERSEDIA]` |
| FR-BIO-02 | Disimpan sebagai embedding (bukan gambar mentah) | W | `[TERSEDIA]` (ArcFace 512-d, array `encodings`) |
| FR-BIO-03 | Pencocokan 1:N dengan ambang batas | W | `[TERSEDIA]` (cosine similarity di Go, default 0.45, pose terbaik) |
| FR-BIO-04 | Hasil cocok menghasilkan identitas kandidat + skor keyakinan | W | `[TERSEDIA]` |
| FR-BIO-05 | Ambang batas dikalibrasi terhadap data nyata (FRR/FAR) | W | `[BELUM]` |
| FR-BIO-06 | Pencocokan dapat dibatasi ke kandidat kelas terkait | S | `[BELUM]` |
| FR-BIO-07 | Enrollment tercatat untuk audit (siapa, kapan, hasil) | W | `[BELUM]` |
| FR-BIO-08 | Persetujuan (consent), masa retensi, dan prosedur penghapusan biometrik | W | `[SEBAGIAN]` (retensi otomatis foto scan 30/90 hari; penghapusan data biometrik per mahasiswa tersedia lewat hapus mahasiswa; consent belum) |
| FR-BIO-09 | Template biometrik dienkripsi saat disimpan | W | `[BELUM]` |
| FR-BIO-10 | Satu wajah tidak dapat didaftarkan pada dua NIM berbeda | S | `[TERSEDIA]` |
| FR-BIO-11 | Foto enrollment (semua pose) & scan disimpan di object storage privat dan hanya dapat dilihat staf berwenang (dosen: kelasnya) | S | `[TERSEDIA]` (Garage; tautan foto di halaman Mahasiswa & Log) |

##### 3.9 API integrasi

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-API-01 | API JSON untuk dashboard/kelas/laporan, dibatasi role | W | `[TERSEDIA]` |
| FR-API-02 | API pendaftaran mahasiswa & enrollment (administrator) | W | `[TERSEDIA]` |
| FR-API-03 | API untuk perangkat (scan & health) | W | `[TERSEDIA]` |
| FR-API-04 | Versi API ditandai pada path | W | `[SEBAGIAN]` (`/api/v1`) |
| FR-API-05 | Dokumentasi mesin (OpenAPI/Swagger) | S | `[BELUM]` |
| FR-API-06 | Format error konsisten (kode + pesan) | W | `[TERSEDIA]` |
| FR-API-07 | Rate limit terpisah per jenis klien | S | `[SEBAGIAN]` (baru untuk login) |

##### 3.10 Log pemindaian

| ID | Kebutuhan | Pri | Status |
| --- | --- | --- | --- |
| FR-LOG-01 | Menampilkan riwayat scan seluruh waktu (200 terbaru) dalam cakupan role | S | `[TERSEDIA]` |
| FR-LOG-02 | Menyaring log berdasarkan status dan kelas | S | `[TERSEDIA]` |
| FR-LOG-03 | Mencari log berdasarkan nama atau NIM mahasiswa | S | `[TERSEDIA]` |
| FR-LOG-04 | Ringkasan jumlah total/terverifikasi/ditinjau/ditolak seluruh waktu | S | `[TERSEDIA]` |
| FR-LOG-05 | Log dapat dicetak | B | `[TERSEDIA]` |
| FR-LOG-06 | Paginasi untuk melihat lebih dari 200 event | S | `[BELUM]` |

#### 4. Kebutuhan Antarmuka Eksternal

##### 4.1 Antarmuka pengguna

| ID | Kebutuhan | Status |
| --- | --- | --- |
| UI-01 | Bahasa Indonesia | `[TERSEDIA]` |
| UI-02 | Tata letak responsif + menu untuk layar kecil | `[TERSEDIA]` |
| UI-03 | Navigasi antar halaman tanpa reload penuh | `[TERSEDIA]` |
| UI-04 | Umpan balik aksi: notifikasi (toast) & dialog | `[TERSEDIA]` |
| UI-05 | Halaman dapat dicetak dengan tata letak khusus | `[TERSEDIA]` |
| UI-06 | Mudah diakses keyboard/screen reader | `[SEBAGIAN]` |
| UI-07 | `ISI:` pedoman merek/desain institusi | TBD |

##### 4.2 Antarmuka perangkat lunak

| ID | Antarmuka | Detail | Status |
| --- | --- | --- | --- |
| IF-SW-01 | Backend ↔ Layanan wajah | HTTP internal bertoken; `GET /health`, `POST /embed` (scan), `POST /register` (satu request per pose secara paralel saat enrollment; dua inferensi bersamaan) → embedding 512-d; endpoint manajemen model hanya melalui backend Go | `[TERSEDIA]` |
| IF-SW-02 | Backend ↔ Basis data | MongoDB (driver Go resmi v1.17) | `[TERSEDIA]` |
| IF-SW-05 | Penyimpanan template wajah | Go menulis `face_templates`; layanan wajah stateless | `[TERSEDIA]` |
| IF-SW-03 | Browser ↔ Backend | HTML/htmx + JSON `/api/v1` | `[TERSEDIA]` |
| IF-SW-06 | Backend ↔ Object storage | S3 API (SigV4, gaya path) ke Garage; `PUT/GET/DELETE` objek, `HEAD` bucket | `[TERSEDIA]` (opsional) |
| IF-SW-04 | `ISI:` Backend ↔ SIAKAD/SSO kampus | ISI: | TBD |

##### 4.3 Antarmuka perangkat keras

| ID | Kebutuhan | Status |
| --- | --- | --- |
| IF-HW-01 | Perangkat kamera mengirim gambar multi-frame via HTTP multipart ke backend (`POST /api/v1/scans`) | `[SEBAGIAN]` (firmware `esp32_scanner` + `esp32cam_uart` ada dan lolos kompilasi; belum diuji di perangkat keras) |
| IF-HW-04 | Perangkat mengirim heartbeat HTTP berkala (< 2 menit) | `[TERSEDIA]` (ESP32 `esp32_scanner`, ESP8266) |
| IF-HW-05 | ESP32 utama ↔ ESP32-CAM melalui UART (`ON`/`CAP`/`OFF`, JPEG + CRC32); Wi-Fi hanya commissioning | `[SEBAGIAN]` (firmware ada; commissioning perangkat keras belum dituntaskan) |
| IF-HW-02 | `ISI:` spesifikasi minimum kamera/resolusi & pencahayaan | TBD |
| IF-HW-03 | `ISI:` kebutuhan daya & jaringan di lokasi | TBD |

##### 4.4 Antarmuka komunikasi

| ID | Kebutuhan | Status |
| --- | --- | --- |
| IF-CM-01 | HTTPS/TLS di produksi | `[BELUM]` (belum ditegakkan kode) |
| IF-CM-02 | Header keamanan (CSP, nosniff, frame-ancestors, dsb.) | `[TERSEDIA]` |
| IF-CM-03 | `ISI:` perlu tidaknya jaringan terisolasi untuk perangkat | TBD |

#### 5. Kebutuhan Non-Fungsional

##### 5.1 Performa

| ID | Kebutuhan | Target | Status |
| --- | --- | --- | --- |
| NFR-PERF-01 | Batas ukuran gambar yang diterima | 8 MB | `[TERSEDIA]` |
| NFR-PERF-02 | Timeout panggilan ke layanan wajah | `/embed` 15 detik; enrollment 15 detik per gelombang inferensi, maksimal 45 detik untuk 6 foto | `[TERSEDIA]` |
| NFR-PERF-03 | Waktu muat halaman dashboard | `ISI:` (mis. < 2 s pada LAN) | TBD |
| NFR-PERF-04 | Throughput scan bersamaan | `ISI:` (mis. N per menit) | TBD |
| NFR-PERF-05 | Latensi checkout absensi | `ISI:` | TBD |
| NFR-PERF-06 | Batas besar body request JSON | 1 MB | `[TERSEDIA]` |

##### 5.2 Keamanan

| ID | Kebutuhan | Status |
| --- | --- | --- |
| NFR-SEC-01 | CSP `script-src 'self'`, tanpa inline script | `[TERSEDIA]` |
| NFR-SEC-02 | Cookie sesi HttpOnly, SameSite=Lax, Secure (aplikasi hanya berjalan di production) | `[TERSEDIA]` |
| NFR-SEC-03 | Perlindungan CSRF untuk semua aksi tulis staf | `[TERSEDIA]` |
| NFR-SEC-04 | Secret wajib ≥ 32 karakter; server menolak start bila kurang | `[TERSEDIA]` |
| NFR-SEC-05 | Perbandingan token memakai waktu konstan | `[TERSEDIA]` |
| NFR-SEC-06 | HSTS | `[BELUM]` |
| NFR-SEC-07 | Session store & rate limiter bersama (multi-instance) | `[BELUM]` |
| NFR-SEC-08 | Jejak audit (login, enrollment, perubahan data) | `[BELUM]` |
| NFR-SEC-09 | Pemindaian dependensi / pembaruan berkala | `ISI:` |
| NFR-SEC-10 | Penanganan panic tanpa membocorkan detail internal | `[TERSEDIA]` |
| NFR-SEC-11 | Kredensial (Wi-Fi, token) tidak tertulis di kode firmware/repo | `[BELUM]` |
| NFR-SEC-12 | Heartbeat perangkat memerlukan token dan health endpoint tidak mengubah perangkat | `[TERSEDIA]` |

##### 5.3 Privasi & kepatuhan

| ID | Kebutuhan | Status |
| --- | --- | --- |
| NFR-PRIV-01 | Template wajah tidak dikirim ke browser; foto hanya untuk staf berwenang | `[TERSEDIA]` |
| NFR-PRIV-02 | Foto tidak disimpan permanen | `[SEBAGIAN]` (foto scan berretensi 30/90 hari; foto enrollment disimpan selama template ada; dapat dimatikan dengan mengosongkan `S3_*`) |
| NFR-PRIV-03 | Persetujuan (consent) penggunaan biometrik | `[BELUM]` |
| NFR-PRIV-04 | Masa retensi & penghapusan data biometrik | `[TERSEDIA]` (retensi otomatis foto scan 30/90 hari di Garage; penghapusan mahasiswa oleh administrator menghapus template wajah, foto enrollment, foto scan, dan log scan terkait secara menyeluruh) |
| NFR-PRIV-05 | Enkripsi at-rest untuk template & backup | `[BELUM]` |
| NFR-PRIV-06 | `ISI:` dasar hukum & kebijakan privasi institusi (mis. UU PDP) | TBD |

##### 5.4 Keandalan & ketersediaan

| ID | Kebutuhan | Status |
| --- | --- | --- |
| NFR-REL-01 | Pematian anggun (graceful shutdown) | `[TERSEDIA]` (8 detik) |
| NFR-REL-02 | Skema & seed idempoten saat startup | `[TERSEDIA]` |
| NFR-REL-03 | Perilaku saat layanan wajah mati: scan tidak dicatat sebagai hadir | `[TERSEDIA]` |
| NFR-REL-07 | Object storage mati tidak menghentikan absensi maupun enrollment | `[TERSEDIA]` |
| NFR-REL-04 | Backup basis data terjadwal & teruji | `[BELUM]` |
| NFR-REL-05 | Target ketersediaan | `ISI:` (mis. 99% jam kerja) | TBD |
| NFR-REL-06 | Pemulihan setelah kegagalan (RTO/RPO) | `ISI:` | TBD |

##### 5.5 Pemeliharaan & keterlacakan

| ID | Kebutuhan | Status |
| --- | --- | --- |
| NFR-MAINT-01 | Satu sumber query/validasi untuk halaman dan API | `[TERSEDIA]` |
| NFR-MAINT-02 | Dokumentasi arsitektur, kode, API, handoff | `[TERSEDIA]` |
| NFR-MAINT-03 | Test otomatis (unit/integrasi) | `[BELUM]` (suite otomatis dihapus dari repository; verifikasi rilis memerlukan build, pemeriksaan statis, dan commissioning terkontrol) |
| NFR-MAINT-04 | Logging terstruktur & observability | `[BELUM]` |
| NFR-MAINT-05 | Migrasi skema berversi | `[BELUM]` |
| NFR-MAINT-06 | CI (lint dan build) | `[BELUM]` |

##### 5.6 Portabilitas

| ID | Kebutuhan | Status |
| --- | --- | --- |
| NFR-PORT-01 | Dapat berjalan pada platform server produksi yang disetujui | `ISI:` |
| NFR-PORT-02 | Basis data produksi MongoDB dengan autentikasi & TLS | `[SEBAGIAN]` (koneksi ada; hardening manual sesuai README) |
| NFR-PORT-03 | Konfigurasi via environment, tanpa mengubah kode | `[TERSEDIA]` |

##### 5.7 Kegunaan & aksesibilitas

| ID | Kebutuhan | Status |
| --- | --- | --- |
| NFR-USA-01 | Umpan balik jelas untuk aksi gagal/berhasil | `[TERSEDIA]` |
| NFR-USA-02 | Pesan kesalahan berbahasa Indonesia & dapat ditindaklanjuti | `[TERSEDIA]` |
| NFR-USA-03 | `ISI:` target perangkat & browser yang didukung | TBD |

#### 6. Batasan & Kepatuhan

| ID | Batasan | Dampak |
| --- | --- | --- |
| CN-01 | Data biometrik termasuk data pribadi sensitif | Perlu dasar hukum, transparansi, kontrol akses, retensi |
| CN-02 | Liveness belum tervalidasi | Hasil absensi berisiko spoofing; wajib ditinjau manusia |
| CN-03 | Ambang pencocokan belum dikalibrasi | Risiko false accept/reject |
| CN-04 | Session & rate limit in-memory | Tidak aman untuk multi-instance |
| CN-05 | Satu token perangkat global | Bocor satu = bocor semua |
| CN-07 | Face service hanya dapat diakses melalui bearer token dan jaringan internal | Token wajib disamakan dengan konfigurasi backend Go |
| CN-08 | Foto wajah tersimpan di object storage | Bucket privat, kunci khusus bucket, port RPC Garage dibatasi |
| CN-06 | `ISI:` kepatuhan lain (akreditasi, audit internal) | ISI: |

#### 7. Matriks Keterlacakan (ringkas)

| Area | Jumlah FR | `[TERSEDIA]` | `[SEBAGIAN]` | `[BELUM]` | TBD |
| --- | --- | --- | --- | --- | --- |
| Autentikasi & sesi | 11 | 8 | 1 | 1 | 1 |
| Otorisasi | 7 | 5 | 1 | 1 | 0 |
| Dashboard | 7 | 6 | 0 | 1 | 0 |
| Kelas | 3 | 2 | 0 | 1 | 0 |
| Mahasiswa & enrollment | 12 | 10 | 0 | 2 | 0 |
| Laporan | 9 | 5 | 0 | 3 | 1 |
| Perangkat & absensi otomatis | 16 | 11 | 0 | 5 | 0 |
| Pengenalan wajah | 11 | 6 | 1 | 4 | 0 |
| API integrasi | 7 | 4 | 2 | 1 | 0 |
| Log pemindaian | 6 | 5 | 0 | 1 | 0 |
| **Total** | **89** | **62** | **5** | **20** | **2** |

> Angka di atas dihitung dari tabel Bagian 3 pada dokumen ini (revisi 0.7,
> 29 Oktober 2026); perbarui bila kebutuhan berubah.

#### 8. Daftar Keputusan yang Diperlukan (TBD)

1. ~~Basis data produksi~~ — **diputuskan: MongoDB** (lihat ADR-6). Sisa: topologi (replica set?) dan kebijakan backup.
2. Target performa & ketersediaan (NFR-PERF-03/04, NFR-REL-05/06).
3. Regulasi privasi yang mengikat & kebijakan retensi biometrik (default teknis saat ini: foto scan 30 hari untuk `Hadir`, 90 hari lainnya — perlu disahkan).
4. Perlu integrasi SIAKAD/SSO kampus?
5. Jumlah perangkat & lokasi pemasangan.
6. Apakah mahasiswa perlu melihat rekap absensinya sendiri (media lain)?
7. SLA dukungan & siapa pemilik operasional sistem.


## Standar dokumentasi

#### Konvensi penulisan dokumen

- Bahasa: Indonesia, istilah teknis dipertahankan apa adanya.
- Semua klaim tentang kode dapat dilacak ke berkas/fungsi; sebutkan nama
  berkas saat merujuk perilaku.
- Bedakan jelas antara **yang sudah ada di kode** dan **usulan/belum ada**.
  Bagian ini menandai status dengan `[TERSEDIA]`, `[SEBAGIAN]`,
  `[BELUM]`.
- Bila kode berubah, perbarui dokumen terkait pada perubahan yang sama dan
  catat tanggalnya di bagian status `panduan operasional`.
