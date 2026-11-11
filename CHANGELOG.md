# Changelog NgirimPC

## v2.0.0-alpha.1 - 2026-11-11

### Baru
- Update otomatis dari GitHub Releases (https://github.com/UdyneO2/NgirimPC/releases/): cek rilis, unduh `NgirimPC.exe`, verifikasi SHA-256 + ukuran + header program, lalu pasang dan jalankan ulang
- Rilis tanpa checksum tidak pernah dipasang; hanya HTTPS yang diizinkan
- Pra-rilis (alpha/beta/rc) hanya ditawarkan bila opsi pra-rilis dinyalakan
- Workflow GitHub Actions: build Windows x64 (CRT statis, satu exe), jalankan tes, lalu terbitkan `NgirimPC.exe` dan `NgirimPC.exe.sha256`

### Catatan
- Protokol jaringan kompatibel dengan Ngirim Android v2.0 (protokol v2)
- Exe belum ditandatangani (code signing): Windows SmartScreen dapat menampilkan peringatan pada unduhan pertama
