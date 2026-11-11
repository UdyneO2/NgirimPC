// Antarmuka platform: semua yang butuh Win32 ada di balik header ini supaya UI (src/app) portabel.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace plat {

// ---- dialog dan shell
std::vector<std::string> pickFiles();                    // kosong bila dibatalkan
std::string pickFolder(const std::string& title);        // "" bila dibatalkan
void openInExplorer(const std::string& path);            // tampilkan file/folder di Explorer
void openPath(const std::string& path);                  // buka dengan aplikasi bawaan
void openUrl(const std::string& url);
bool recycle(const std::vector<std::string>& paths, std::string& err);  // ke Tempat Sampah

// ---- lokasi
std::string knownFolder(const std::string& which);       // desktop, documents, downloads, pictures, videos, music, home
std::vector<std::string> drives();                       // "C:\\", "D:\\", ...
std::string deviceName();                                // nama komputer
std::string dataDir();                                   // %APPDATA%\NgirimPC (dibuat bila belum ada)
bool fileExists(const std::string& path);
bool systemPrefersDark();

// ---- pembaruan
bool httpGet(const std::string& url, std::string& body, std::string& err);
bool httpDownload(const std::string& url, const std::string& dest,
                  const std::function<void(int64_t done, int64_t total)>& progress,
                  const std::atomic<bool>& cancel, std::string& err);
std::string tempDir();
bool applyUpdate(const std::string& newExe, std::string& err);  // ganti exe berjalan, jalankan ulang
void requestQuit();                                      // aman dipanggil dari thread mana pun

}  // namespace plat

namespace app {
void init(float dpiScale);
void frame();
void shutdown();
void onDrop(const std::vector<std::string>& paths);
}  // namespace app
