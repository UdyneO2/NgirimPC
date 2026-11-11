# Changelog NgirimPC

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
