package controller

import (
	"encoding/json"
	"errors"
	"html/template"
	"io"
	"log"
	"net/http"
	"path/filepath"

	"dash.algorithmics/internal/api"
	"github.com/gorilla/csrf"
)

// Absen melayani halaman absen QR untuk mahasiswa (tanpa akun):
// GET /absen?d=&w=&c= dan POST /absen (multipart, dipanggil absen.js).
// Halaman berada di belakang CSRF tetapi tidak memerlukan sesi staf.
type Absen struct {
	page   *template.Template
	server *api.Server
}

type absenPage struct {
	CSRFToken  string
	Error      string
	DeviceID   string
	DeviceName string
	Window     int64
	Code       string
	ExpiresAt  int64 // unix detik
	MaxFrames  int
}

func NewAbsen(server *api.Server, viewDir string) (*Absen, error) {
	page, err := template.ParseFiles(filepath.Join(viewDir, "absen.html"))
	if err != nil {
		return nil, err
	}
	return &Absen{page: page, server: server}, nil
}

func (a *Absen) Page(w http.ResponseWriter, r *http.Request) {
	data := absenPage{CSRFToken: csrf.Token(r), MaxFrames: api.MaxQRFrames}
	session, err := a.server.VerifyQR(r.Context(), r.URL.Query().Get("d"), r.URL.Query().Get("w"), r.URL.Query().Get("c"))
	status := http.StatusOK
	if err != nil {
		data.Error, status = qrErrorMessage(err), http.StatusBadRequest
		if !errors.Is(err, api.ErrQRInvalid) && !errors.Is(err, api.ErrQRExpired) && !errors.Is(err, api.ErrUnknownDevice) {
			log.Printf("absen qr: verifikasi kode gagal: %v", err)
			status = http.StatusInternalServerError
		}
	} else {
		data.DeviceID, data.DeviceName = session.DeviceID, session.DeviceName
		data.Window, data.Code, data.ExpiresAt = session.Window, session.Code, session.ExpiresAt.Unix()
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(status)
	if err := a.page.ExecuteTemplate(w, "absen", data); err != nil {
		log.Printf("absen qr: render: %v", err)
	}
}

func (a *Absen) Submit(w http.ResponseWriter, r *http.Request) {
	r.Body = http.MaxBytesReader(w, r.Body, 12<<20)
	if err := r.ParseMultipartForm(12 << 20); err != nil {
		absenError(w, http.StatusBadRequest, "invalid_request", "Kirim NIM dan swafoto dalam multipart/form-data (maksimal 12 MB).")
		return
	}
	session, err := a.server.VerifyQR(r.Context(), r.FormValue("d"), r.FormValue("w"), r.FormValue("c"))
	if err != nil {
		code := http.StatusBadRequest
		if !errors.Is(err, api.ErrQRInvalid) && !errors.Is(err, api.ErrQRExpired) && !errors.Is(err, api.ErrUnknownDevice) {
			code = http.StatusInternalServerError
		}
		absenError(w, code, "invalid_qr", qrErrorMessage(err))
		return
	}
	frames, err := readQRFrames(r)
	if err != nil {
		absenError(w, http.StatusBadRequest, "invalid_image", err.Error())
		return
	}
	result, err := a.server.QRCheckIn(r.Context(), session, r.FormValue("nim"), frames)
	if err != nil {
		switch {
		case errors.Is(err, api.ErrInvalidNIM):
			absenError(w, http.StatusBadRequest, "invalid_nim", "NIM harus 9 digit angka.")
		case errors.Is(err, api.ErrTooManyAttempts):
			absenError(w, http.StatusTooManyRequests, "too_many_attempts", "Terlalu banyak percobaan untuk NIM ini. Coba lagi 10 menit lagi atau hubungi dosen.")
		case errors.Is(err, api.ErrStudentNotFound), errors.Is(err, api.ErrNotEnrolled):
			// Pesan sama agar halaman publik tidak membocorkan NIM mana yang terdaftar.
			absenError(w, http.StatusUnprocessableEntity, "not_registered", "NIM belum terdaftar atau wajah belum direkam. Hubungi administrator.")
		case errors.Is(err, api.ErrNoFace):
			absenError(w, http.StatusUnprocessableEntity, "no_face", "Wajah tidak terlihat. Pastikan wajah berada di dalam bingkai dan cahaya cukup, lalu coba lagi.")
		case errors.Is(err, api.ErrInvalidImage):
			absenError(w, http.StatusBadRequest, "invalid_image", "Foto tidak dapat dibaca. Coba lagi.")
		default:
			log.Printf("absen qr: %v", err)
			absenError(w, http.StatusBadGateway, "verification_unavailable", "Verifikasi wajah sedang tidak tersedia. Absen belum tercatat; coba lagi atau hubungi dosen.")
		}
		return
	}
	writeAbsenJSON(w, http.StatusOK, map[string]any{
		"status": result.Status, "result": result.Result, "message": result.Message,
		"student":   map[string]string{"nim": result.StudentNIM, "name": result.StudentName},
		"classId":   result.ClassID,
		"scannedAt": result.ScannedAt,
	})
}

func readQRFrames(r *http.Request) ([][]byte, error) {
	headers := r.MultipartForm.File["image"]
	if len(headers) == 0 || len(headers) > api.MaxQRFrames {
		return nil, errors.New("kirim 1–3 swafoto")
	}
	frames := make([][]byte, 0, len(headers))
	for _, header := range headers {
		file, err := header.Open()
		if err != nil {
			return nil, errors.New("swafoto tidak dapat dibaca")
		}
		data, err := io.ReadAll(io.LimitReader(file, 4<<20+1))
		file.Close()
		if err != nil || len(data) == 0 || len(data) > 4<<20 {
			return nil, errors.New("swafoto kosong atau melebihi 4 MB")
		}
		frames = append(frames, data)
	}
	return frames, nil
}

func qrErrorMessage(err error) string {
	switch {
	case errors.Is(err, api.ErrQRExpired):
		return "Kode QR sudah kedaluwarsa. Pindai ulang QR di layar kelas."
	case errors.Is(err, api.ErrQRInvalid), errors.Is(err, api.ErrUnknownDevice):
		return "Kode QR tidak valid. Pindai QR yang tampil di layar kelas."
	}
	return "Kode QR belum dapat diperiksa. Coba lagi sebentar."
}

func absenError(w http.ResponseWriter, status int, code, message string) {
	writeAbsenJSON(w, status, map[string]any{"error": map[string]string{"code": code, "message": message}})
}

func writeAbsenJSON(w http.ResponseWriter, status int, data any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(data)
}
