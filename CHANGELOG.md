# Changelog NgirimPC

## v2.10.0-alpha.1 - 2026-11-13

Menerapkan Ngirim Android v2.10 (commit 5df9a99), 5 fase. Protokol tidak berubah di v2.10.

### Baru (fase 1-4: tab dan antrean)
- Bilah tab menyatu dengan bilah judul (tanpa garis pemisah) dan tanpa geser: tab terbuka berbagi lebar, label dipotong lalu hilang bila sempit
- Tab bisa ditutup dengan tombol X dan berubah menjadi ikon saja; klik ikonnya untuk membukanya lagi
- Banyak file manager: tombol + membuka tab file manager baru (maksimal 6) dengan folder masing-masing; clipboard dipakai bersama sehingga salin di satu tab bisa ditempel di tab lain; tab file manager terakhir tidak bisa ditutup
- Antrean multi-perangkat: pairing beberapa perangkat, centang yang ingin dikirimi, file dikirim berurutan dengan status per perangkat (Menunggu, Mengirim, Selesai, Gagal), tombol Ulangi yang gagal; klik nama perangkat untuk memakainya di tab FTP

## v2.9.0-alpha.1 - 2026-11-12

Sinkronisasi dengan Ngirim Android v2.9: fase 1 (protokol), fase 2 (fitur), fase 3 (tampilan dan installer).

### Baru
- Installer `NgirimPC-Setup-<tag>.exe` (per-pengguna, tanpa admin, folder instalasi bisa ditulis sehingga update otomatis tetap jalan) di samping exe portabel
- Tampilan v2.9: bilah judul biru (gelap di tema gelap) dengan versi dan status terima, bilah status di bawah (folder, jumlah terpilih, clipboard), panel bersudut 4px, palet baru, font Source Sans 3 (OFL) tertanam di exe
- Pairing lewat QR: di tab Wi-Fi, saat Terima aktif, tombol Tampilkan QR menampilkan kode `ngirim://pair?...` yang dipindai Ngirim Android atau kamera; pilihan alamat IP bila ada beberapa jaringan
- Klik kanan di file manager (Buka, Salin, Potong, Tempel, Ubah nama, Hapus, Kirim) dan pintasan keyboard (Ctrl+C/X/V/A, Del, F2, F5, Backspace)
- Bahasa Indonesia dan Inggris, otomatis mengikuti bahasa Windows; bisa dipilih di Pengaturan > Bahasa
- Perbaikan pembuat QR: modul pola waktu tidak lagi tertimpa area format (sebelumnya sebagian kode versi 5 tidak terbaca oleh pembaca QR OpenCV)
- Protokol jaringan v3 (Ngirim Android v2.5 ke atas): manifest membawa waktu ubah file, penerima membalas offset lanjutan
- Transfer yang putus bisa dilanjutkan: penerima menyimpan sisa sebagai `<nama>.<ukuran>-<waktu>.ngpart`, dan saat file yang sama dikirim ulang hanya sisanya yang dikirim. Berlaku di semua arah (kirim, unggah, unduh)
- Waktu ubah file asli dipertahankan di penerima
- Koneksi putus di tengah jalan dicoba lanjut otomatis (sampai 4 kali), begitu pula bila penerima sedang sibuk; PIN salah dan penolakan lain tidak diulang
- Sisa `.ngpart` yang tidak pernah dilanjutkan dihapus otomatis setelah 7 hari saat penerimaan diaktifkan

### Perubahan
- Kompatibel dengan Ngirim Android v2.5 sampai v2.9. Versi Android lama (protokol v2) menampilkan "Versi protokol berbeda"; perbarui kedua perangkat

## v2.0.0-alpha.1 - 2026-11-11

### Baru
- Wi-Fi: alamat IP:port komputer ditampilkan saat menerima (tombol Salin alamat), tombol Putuskan pairing
- Fitur: file manager (salin, potong, tempel, hapus ke Tempat Sampah atau permanen, ganti nama, folder baru), Wi-Fi (terima dengan PIN, deteksi perangkat mDNS, pairing, kirim file/folder, tarik-lepas), FTP dua panel (jelajah, unduh, unggah), progres dengan kecepatan dan sisa waktu
- Kerangka GUI (Dear ImGui + Direct3D 11): tab Lokal (jelajah baca-saja), Wi-Fi, FTP, Pengaturan, Log; tema Terang/Gelap/Sistem; banner pembaruan
- Update otomatis dari GitHub Releases (https://github.com/UdyneO2/NgirimPC/releases/): cek rilis, unduh `NgirimPC.exe`, verifikasi SHA-256 + ukuran + header program, lalu pasang dan jalankan ulang
- Rilis tanpa checksum tidak pernah dipasang; hanya HTTPS yang diizinkan
- Pra-rilis (alpha/beta/rc) hanya ditawarkan bila opsi pra-rilis dinyalakan
- Workflow GitHub Actions: build Windows x64 (CRT statis, satu exe), lalu terbitkan `NgirimPC-<tag>.exe` dan `NgirimPC-<tag>.exe.sha256` (nama mengikuti tag)

### Catatan
- Protokol jaringan kompatibel dengan Ngirim Android v2.0 (protokol v2)
- Exe belum ditandatangani (code signing): Windows SmartScreen dapat menampilkan peringatan pada unduhan pertama
