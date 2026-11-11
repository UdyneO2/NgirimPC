# NgirimPC

Versi Windows 10/11 dari Ngirim: kirim file, folder, APK, dan data game antar PC dan Android lewat Wi-Fi/LAN.
Kompatibel dengan Ngirim Android v2.0. Satu file `NgirimPC.exe`, tanpa instalasi.

## Memakai
1. Unduh `NgirimPC-vX.Y.Z.exe` (mis. `NgirimPC-v2.0.0.exe`) dari [Releases](https://github.com/UdyneO2/NgirimPC/releases/) dan taruh di folder yang bisa ditulis (Desktop atau Documents, bukan Program Files), supaya update otomatis bisa mengganti file.
2. **Terima di PC:** tab Wi-Fi, aktifkan penerimaan, lalu masukkan alamat dan PIN yang tampil di Ngirim Android.
3. **Kirim dari PC:** tab Wi-Fi, pilih perangkat Android yang terdeteksi (atau isi IP:port), isi PIN dari HP, tekan Pairing, tambahkan file (atau tarik-lepas), tekan Kirim.
4. **Jelajah folder HP:** setelah pairing, tab FTP menampilkan PC di kiri dan Android di kanan.

Catatan keamanan: data Wi-Fi tidak dienkripsi (otentikasi PIN saja). Pakai hanya di jaringan tepercaya.
Pertama kali menerima, izinkan Windows Firewall untuk jaringan pribadi. Router dengan client isolation memblokir koneksi antar-perangkat.

## Update otomatis
Saat dibuka, aplikasi mengecek rilis terbaru di GitHub. Unduhan hanya dipasang bila SHA-256 dan ukurannya cocok dan berupa program Windows. Pra-rilis (alpha/beta/rc) hanya ditawarkan bila dinyalakan di Pengaturan. Exe belum ditandatangani, jadi SmartScreen bisa memberi peringatan pada unduhan pertama.

## Build
```
cmake -S . -B build -A x64 -DNGIRIMPC_VERSION=2.0.0
cmake --build build --config Release
```
Tes unit (opsional): tambahkan `-DNGIRIMPC_BUILD_TESTS=ON`, lalu `ctest --test-dir build -C Release`.

Rilis: push tag `vX.Y.Z` (contoh `v2.0.0`); GitHub Actions membangun dan menerbitkan `NgirimPC-<tag>.exe` dan `NgirimPC-<tag>.exe.sha256`.

## Lisensi
MIT, lihat `LICENSE`. Antarmuka memakai [Dear ImGui](https://github.com/ocornut/imgui) (MIT, Omar Cornut dan kontributor).
