package api

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"log"
	"strconv"
	"strings"
	"sync"
	"time"

	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
)

// Absensi lewat QR (GET/POST /absen).
//
// Layar QR (ESP32 + TFT, devices/esp32_qr_display) menampilkan tautan
//
//	{BASE_URL}/absen?d={device_id}&w={window}&c={code}
//
// window = floor(unix / 30) dan code = 16 karakter hex pertama dari
// HMAC-SHA256(DEVICE_TOKEN, "qr\n" + device_id + "\n" + window). Kode berganti
// tiap 30 detik sehingga foto QR yang dibagikan cepat kedaluwarsa. Server
// menerima window saat ini sampai QRAcceptWindows-1 window sebelumnya (waktu
// untuk mengisi NIM dan swafoto) plus satu window ke depan (selisih jam).
//
// Mahasiswa mengisi NIM dan mengambil 2–3 swafoto. Go mencocokkan frame 1:N
// seperti scan perangkat (matchFrames + decideFrames), lalu mensyaratkan
// mahasiswa yang cocok SAMA dengan NIM yang diisi. Ini tetap BUKAN
// anti-spoof: QR yang diteruskan dalam masa berlakunya bisa dipakai dari
// tempat lain oleh pemilik wajah itu sendiri.

const (
	QRWindow        = 30 * time.Second
	QRAcceptWindows = 4 // window saat ini + 3 sebelumnya (±2 menit)
	qrCodeLength    = 16
	// MaxQRFrames membatasi swafoto per absen QR.
	MaxQRFrames = 3
	// Batas percobaan absen QR per NIM (in-memory, satu instance).
	qrAttemptsPerNIM = 6
	qrAttemptWindow  = 10 * time.Minute
)

// Kode hasil absen QR untuk halaman (field "result").
const (
	QRResultPresent  = "hadir"
	QRResultAlready  = "sudah_hadir"
	QRResultReview   = "perlu_ditinjau"
	QRResultMismatch = "wajah_tidak_sesuai"
)

var (
	ErrQRInvalid       = errors.New("kode QR tidak valid")
	ErrQRExpired       = errors.New("kode QR kedaluwarsa")
	ErrUnknownDevice   = errors.New("perangkat tidak terdaftar")
	ErrNotEnrolled     = errors.New("wajah mahasiswa belum terdaftar")
	ErrTooManyAttempts = errors.New("terlalu banyak percobaan")
)

// QRSession adalah kode QR yang sudah diverifikasi.
type QRSession struct {
	DeviceID   string
	DeviceName string
	Window     int64
	Code       string
	ExpiresAt  time.Time
}

// QRResult adalah hasil satu absen QR.
type QRResult struct {
	Status      string
	Result      string
	Message     string
	StudentNIM  string
	StudentName string
	ClassID     string
	ScannedAt   string
	Confidence  float64
}

// QRWindowAt mengembalikan nomor window QR untuk waktu t.
func QRWindowAt(t time.Time) int64 { return t.Unix() / int64(QRWindow/time.Second) }

// QRCode menghitung kode QR untuk satu perangkat dan window. Algoritmanya
// harus sama dengan firmware devices/esp32_qr_display.
func QRCode(deviceToken, deviceID string, window int64) string {
	mac := hmac.New(sha256.New, []byte(deviceToken))
	_, _ = mac.Write([]byte("qr\n" + deviceID + "\n" + strconv.FormatInt(window, 10)))
	return hex.EncodeToString(mac.Sum(nil))[:qrCodeLength]
}

// checkQRCode memvalidasi format, tanda tangan, dan masa berlaku kode.
func checkQRCode(deviceToken, deviceID, windowRaw, code string, now time.Time) (int64, error) {
	if deviceID == "" || len(deviceID) > 64 || len(code) != qrCodeLength || deviceToken == "" {
		return 0, ErrQRInvalid
	}
	window, err := strconv.ParseInt(windowRaw, 10, 64)
	if err != nil || window <= 0 {
		return 0, ErrQRInvalid
	}
	expected := QRCode(deviceToken, deviceID, window)
	if !hmac.Equal([]byte(strings.ToLower(code)), []byte(expected)) {
		return 0, ErrQRInvalid
	}
	current := QRWindowAt(now)
	if window > current+1 || window <= current-QRAcceptWindows {
		return 0, ErrQRExpired
	}
	return window, nil
}

// qrExpiry adalah batas akhir kode window tertentu diterima server.
func qrExpiry(window int64) time.Time {
	return time.Unix((window+QRAcceptWindows)*int64(QRWindow/time.Second), 0).UTC()
}

// VerifyQR memeriksa kode QR dan memastikan perangkatnya terdaftar.
func (s *Server) VerifyQR(ctx context.Context, deviceID, windowRaw, code string) (QRSession, error) {
	deviceID = strings.TrimSpace(deviceID)
	code = strings.ToLower(strings.TrimSpace(code))
	window, err := checkQRCode(s.config.DeviceToken, deviceID, strings.TrimSpace(windowRaw), code, time.Now())
	if err != nil {
		return QRSession{}, err
	}
	var device struct {
		Name string `bson:"name"`
	}
	if err := s.db.Collection("devices").FindOne(ctx, bson.M{"_id": deviceID}).Decode(&device); err != nil {
		if errors.Is(err, mongo.ErrNoDocuments) {
			return QRSession{}, ErrUnknownDevice
		}
		return QRSession{}, err
	}
	if device.Name == "" {
		device.Name = deviceID
	}
	return QRSession{DeviceID: deviceID, DeviceName: device.Name, Window: window, Code: code, ExpiresAt: qrExpiry(window)}, nil
}

// qrLimiter membatasi percobaan absen per NIM. Pembatasan per IP saja tidak
// cukup karena satu kelas biasanya berbagi IP NAT kampus.
type qrLimiter struct {
	sync.Mutex
	byNIM map[string]attemptWindow
}

type attemptWindow struct {
	started time.Time
	count   int
}

func (l *qrLimiter) allow(nim string, now time.Time) bool {
	l.Lock()
	defer l.Unlock()
	if l.byNIM == nil {
		l.byNIM = make(map[string]attemptWindow)
	}
	state := l.byNIM[nim]
	if state.started.IsZero() || now.Sub(state.started) >= qrAttemptWindow {
		state = attemptWindow{started: now}
	}
	state.count++
	l.byNIM[nim] = state
	if len(l.byNIM) > 20000 {
		for key, entry := range l.byNIM {
			if now.Sub(entry.started) >= qrAttemptWindow {
				delete(l.byNIM, key)
			}
		}
	}
	return state.count <= qrAttemptsPerNIM
}

// qrDecision menentukan status absen QR dari verdict pencocokan dan NIM
// yang diklaim. recorded=false berarti tidak ada event yang disimpan (mis.
// wajah tidak terlihat; mahasiswa cukup mengulang).
func qrDecision(verdict ScanVerdict, claimedID string, minMatch int) (status, result, reason string, recorded bool) {
	switch {
	case verdict.FramesWithFace == 0:
		return "", "", "", false
	case verdict.StudentID == claimedID && verdict.FramesMatched >= minMatch:
		return "Hadir", QRResultPresent, "", true
	case verdict.StudentID == claimedID:
		return "Perlu ditinjau", QRResultReview, fmt.Sprintf(
			"Absen QR: wajah cocok di %d dari %d foto; minimal %d foto agar langsung hadir.",
			verdict.FramesMatched, verdict.FramesTotal, minMatch), true
	case verdict.StudentID != "":
		return "Ditolak", QRResultMismatch, "Absen QR: wajah cocok dengan mahasiswa lain, bukan pemilik NIM.", true
	default:
		reason = "Absen QR: wajah tidak sesuai dengan NIM."
		if verdict.Reason != "" {
			reason = "Absen QR: " + verdict.Reason
		}
		return "Ditolak", QRResultMismatch, reason, true
	}
}

// QRCheckIn memproses absen QR: NIM → mahasiswa & template → pencocokan
// frame → keputusan → event scan_events (source "qr"). Mahasiswa yang sudah
// Hadir hari ini tidak dicatat dua kali.
func (s *Server) QRCheckIn(ctx context.Context, session QRSession, nim string, frames [][]byte) (QRResult, error) {
	nim = strings.TrimSpace(nim)
	if !ValidNIM(nim) {
		return QRResult{}, ErrInvalidNIM
	}
	if !s.qrAttempts.allow(nim, time.Now()) {
		return QRResult{}, ErrTooManyAttempts
	}
	var student struct {
		ID      string `bson:"_id"`
		NIM     string `bson:"nim"`
		Name    string `bson:"name"`
		ClassID string `bson:"class_id"`
	}
	if err := s.db.Collection("students").FindOne(ctx, bson.M{"nim": nim}).Decode(&student); err != nil {
		if errors.Is(err, mongo.ErrNoDocuments) {
			return QRResult{}, ErrStudentNotFound
		}
		return QRResult{}, err
	}
	templates, err := s.loadTemplates(ctx)
	if err != nil {
		return QRResult{}, err
	}
	enrolled := false
	for _, template := range templates {
		if template.StudentID == student.ID {
			enrolled = true
			break
		}
	}
	if !enrolled {
		return QRResult{}, ErrNotEnrolled
	}

	minMatch := min(s.config.ScanMinMatchFrames, len(frames))
	results, err := s.matchFrames(ctx, frames, templates, minMatch)
	if err != nil {
		return QRResult{}, err
	}
	verdict := decideFrames(results, len(frames), s.config.SimThreshold)
	status, result, reason, recorded := qrDecision(verdict, student.ID, minMatch)
	if !recorded {
		return QRResult{}, ErrNoFace
	}
	confidence := verdict.Confidence
	if verdict.StudentID != student.ID {
		confidence = 0 // skor milik orang lain tidak relevan untuk NIM ini
	}

	now := time.Now().UTC()
	out := QRResult{
		Status: status, Result: result, Message: qrMessage(result, reason),
		StudentNIM: student.NIM, StudentName: student.Name, ClassID: student.ClassID,
		ScannedAt: now.Format(time.RFC3339), Confidence: confidence,
	}
	if status == "Hadir" {
		start, end := todayRangeUTC()
		var existing struct {
			ScannedAt string `bson:"scanned_at"`
		}
		err := s.db.Collection("scan_events").FindOne(ctx, bson.M{
			"student_id": student.ID, "status": "Hadir",
			"scanned_at": bson.M{"$gte": start, "$lt": end},
		}, options.FindOne().SetSort(bson.D{{Key: "scanned_at", Value: 1}})).Decode(&existing)
		if err == nil {
			out.Result, out.Message, out.ScannedAt = QRResultAlready, "Sudah tercatat hadir hari ini.", existing.ScannedAt
			return out, nil
		}
		if !errors.Is(err, mongo.ErrNoDocuments) {
			return QRResult{}, err
		}
	}

	event := bson.M{
		"student_id":       student.ID,
		"class_id":         student.ClassID,
		"device_id":        session.DeviceID,
		"source":           "qr",
		"qr_window":        session.Window,
		"scanned_at":       out.ScannedAt,
		"face_verified":    verdict.StudentID == student.ID,
		"liveness_passed":  false,
		"confidence":       confidence,
		"status":           status,
		"review_reason":    reason,
		"frames_total":     verdict.FramesTotal,
		"frames_with_face": verdict.FramesWithFace,
		"frames_matched":   verdict.FramesMatched,
	}
	if s.images != nil {
		evidence := frames[verdict.BestFrame]
		ext, contentType := imageExtension(evidence)
		if key := s.uploadImage(ctx, scanImageKey(session.DeviceID, now, ext), contentType, evidence); key != "" {
			event["image_key"] = key
		}
	}
	if _, err := s.db.Collection("scan_events").InsertOne(ctx, event); err != nil {
		return QRResult{}, err
	}
	log.Printf("absen qr %s nim=%s: %s (%s) foto=%d wajah=%d cocok=%d skor=%.2f",
		session.DeviceID, student.NIM, status, result, verdict.FramesTotal, verdict.FramesWithFace, verdict.FramesMatched, confidence)
	return out, nil
}

func qrMessage(result, reason string) string {
	switch result {
	case QRResultPresent:
		return "Kehadiran tercatat."
	case QRResultReview:
		return "Absen diterima dan menunggu tinjauan dosen. Wajah hanya cocok di sebagian foto."
	case QRResultMismatch:
		return "Wajah tidak sesuai dengan NIM. Percobaan ini dicatat untuk ditinjau."
	}
	return reason
}
