// Halaman absen QR mahasiswa (views/absen.html). Membuka kamera depan,
// mengambil beberapa swafoto berjeda, dan mengirimnya ke POST /absen.
// Tanpa inline script (CSP script-src 'self').
(() => {
  const form = document.getElementById('absenForm');
  if (!form) return;

  const video = document.getElementById('absenVideo');
  const camera = document.getElementById('absenCamera');
  const statusEl = document.getElementById('absenStatus');
  const button = document.getElementById('absenSubmit');
  const nimInput = document.getElementById('absenNim');
  const expiryEl = document.getElementById('absenExpiry');
  const resultEl = document.getElementById('absenResult');
  const csrfToken = document.querySelector('meta[name="csrf-token"]')?.content || '';
  const expiresAt = Number(form.dataset.expires) * 1000;
  const maxFrames = Math.max(1, Number(form.dataset.frames) || 3);
  const NIM_KEY = 'methics.absen.nim';
  const FRAME_GAP_MS = 450;

  let stream = null;
  let busy = false;
  let finished = false;
  let expired = false;

  try {
    const saved = localStorage.getItem(NIM_KEY);
    if (saved && !nimInput.value) nimInput.value = saved;
  } catch { /* penyimpanan browser tidak tersedia */ }

  const setStatus = (text, tone = '') => {
    statusEl.textContent = text;
    statusEl.className = 'absen-notice' + (tone ? ' ' + tone : '');
  };

  const stopCamera = () => {
    if (stream) stream.getTracks().forEach((track) => track.stop());
    stream = null;
  };

  const tickExpiry = () => {
    const left = Math.max(0, Math.round((expiresAt - Date.now()) / 1000));
    expiryEl.textContent = `${Math.floor(left / 60)}:${String(left % 60).padStart(2, '0')}`;
    if (left > 0 || expired || finished) return;
    expired = true;
    button.disabled = true;
    stopCamera();
    setStatus('Kode QR sudah kedaluwarsa. Pindai ulang QR di layar kelas.', 'error');
  };

  const startCamera = async () => {
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) {
      setStatus('Kamera memerlukan koneksi HTTPS dan browser yang mendukung kamera.', 'error');
      return;
    }
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: { facingMode: { ideal: 'user' }, width: { ideal: 1280 }, height: { ideal: 720 } },
      });
      video.srcObject = stream;
      await video.play();
      if (expired) { stopCamera(); return; }
      button.disabled = false;
      setStatus('Posisikan wajah di dalam bingkai dengan cahaya cukup, lalu tekan tombol.');
    } catch (error) {
      const denied = error && (error.name === 'NotAllowedError' || error.name === 'SecurityError');
      setStatus(denied
        ? 'Izin kamera ditolak. Izinkan kamera untuk situs ini di pengaturan browser, lalu muat ulang halaman.'
        : 'Kamera tidak dapat dibuka. Tutup aplikasi lain yang memakai kamera, lalu muat ulang halaman.', 'error');
    }
  };

  const captureFrame = () => {
    const scale = Math.min(1, 960 / video.videoWidth, 960 / video.videoHeight);
    const canvas = document.createElement('canvas');
    canvas.width = Math.round(video.videoWidth * scale);
    canvas.height = Math.round(video.videoHeight * scale);
    canvas.getContext('2d').drawImage(video, 0, 0, canvas.width, canvas.height);
    return new Promise((resolve) => canvas.toBlob(resolve, 'image/jpeg', 0.9));
  };

  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  const formatTime = (value) => {
    const date = new Date(value);
    return Number.isNaN(date.getTime()) ? '' : date.toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit' });
  };

  const tones = { hadir: 'success', sudah_hadir: 'success', perlu_ditinjau: 'warning', wajah_tidak_sesuai: 'error' };
  const titles = {
    hadir: 'Hadir',
    sudah_hadir: 'Sudah hadir',
    perlu_ditinjau: 'Menunggu tinjauan',
    wajah_tidak_sesuai: 'Ditolak',
  };

  const showResult = (body) => {
    finished = true;
    stopCamera();
    form.hidden = true;
    resultEl.hidden = false;
    resultEl.className = 'absen-result ' + (tones[body.result] || 'warning');
    resultEl.replaceChildren();
    const add = (tag, className, text) => {
      if (!text) return;
      const el = document.createElement(tag);
      if (className) el.className = className;
      el.textContent = text;
      resultEl.append(el);
    };
    add('p', 'absen-result-title', titles[body.result] || body.status || 'Selesai');
    add('p', 'absen-result-message', body.message);
    const student = body.student || {};
    add('p', 'absen-result-meta', [student.name, student.nim && `NIM ${student.nim}`].filter(Boolean).join(' · '));
    add('p', 'absen-result-meta', [body.classId, formatTime(body.scannedAt) && `pukul ${formatTime(body.scannedAt)}`].filter(Boolean).join(' · '));
    resultEl.setAttribute('tabindex', '-1');
    resultEl.focus();
  };

  const errorMessage = (response, body) => {
    if (body?.error?.message) return body.error.message;
    if (response.status === 403) return 'Halaman sudah terlalu lama terbuka. Pindai ulang QR di layar kelas.';
    if (response.status === 429) return 'Terlalu banyak percobaan. Tunggu sebentar lalu coba lagi.';
    return 'Absen belum tercatat. Coba lagi.';
  };

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (busy || finished || expired) return;
    const nim = nimInput.value.trim();
    if (!/^\d{9}$/.test(nim)) {
      setStatus('NIM harus 9 digit angka.', 'error');
      nimInput.focus();
      return;
    }
    if (!stream || !video.videoWidth) {
      setStatus('Kamera belum siap. Tunggu pratinjau muncul, lalu coba lagi.', 'error');
      return;
    }

    busy = true;
    button.disabled = true;
    nimInput.readOnly = true;
    camera.classList.add('capturing');
    const data = new FormData(form);
    data.set('nim', nim);
    try {
      for (let i = 0; i < maxFrames; i += 1) {
        setStatus(`Mengambil foto ${i + 1} dari ${maxFrames}… tahan posisi.`);
        const blob = await captureFrame();
        if (!blob) throw new Error('capture');
        data.append('image', blob, `swafoto-${i + 1}.jpg`);
        if (i < maxFrames - 1) await wait(FRAME_GAP_MS);
      }
    } catch {
      setStatus('Foto gagal diambil. Coba lagi.', 'error');
      busy = false;
      button.disabled = expired;
      nimInput.readOnly = false;
      camera.classList.remove('capturing');
      return;
    }
    camera.classList.remove('capturing');
    setStatus('Memverifikasi wajah…');

    try {
      const response = await fetch('/absen', {
        method: 'POST',
        body: data,
        credentials: 'same-origin',
        headers: { 'X-CSRF-Token': csrfToken, Accept: 'application/json' },
      });
      let body = null;
      try { body = await response.json(); } catch { /* bukan JSON (mis. 403 CSRF) */ }
      if (response.ok && body && body.result) {
        try { localStorage.setItem(NIM_KEY, nim); } catch { /* abaikan */ }
        showResult(body);
        return;
      }
      setStatus(errorMessage(response, body), 'error');
    } catch {
      setStatus('Tidak dapat terhubung ke server. Periksa koneksi internet, lalu coba lagi.', 'error');
    } finally {
      busy = false;
      if (!finished) {
        button.disabled = expired;
        nimInput.readOnly = false;
      }
    }
  });

  window.addEventListener('pagehide', stopCamera);
  tickExpiry();
  setInterval(tickExpiry, 1000);
  startCamera();
})();
