// Orkestrasi pembaruan otomatis dari GitHub Releases: cek -> unduh -> verifikasi SHA-256 -> pasang.
// Portabel dan bebas UI: semua akses sistem lewat UpdaterIO (di Windows diisi plat::*), jadi bisa diuji di Linux.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "updater_core.h"

namespace ng {

struct UpdaterIO {
    std::function<bool(const std::string& url, std::string& body, std::string& err)> httpGet;
    std::function<bool(const std::string& url, const std::string& dest,
                       const std::function<void(int64_t done, int64_t total)>& progress,
                       const std::atomic<bool>& cancel, std::string& err)> httpDownload;
    std::function<bool(const std::string& newExe, std::string& err)> applyUpdate;  // ganti exe berjalan + jalankan ulang
    std::function<std::string()> tempDir;
};

enum class UpdState { Idle, Checking, UpToDate, Available, Downloading, Verifying, Ready, Failed };

struct UpdSnapshot {
    UpdState state = UpdState::Idle;
    std::string error;
    UpdateInfo info;          // valid saat Available dan sesudahnya
    int64_t done = 0, total = 0;
    double frac = 0;          // 0..1
};

class Updater {
public:
    // Endpoint daftar rilis; bisa diganti untuk uji.
    static constexpr const char* kReleasesUrl = "https://api.github.com/repos/UdyneO2/NgirimPC/releases?per_page=20";

    Updater(std::string currentVersion, UpdaterIO io, std::string releasesUrl = kReleasesUrl);
    ~Updater();

    void check(bool includePrerelease);   // asinkron
    void download();                      // asinkron; hanya dari state Available
    void cancel();
    UpdSnapshot snapshot();
    bool apply(std::string& err);         // hanya dari state Ready; true = proses baru sudah jalan, segera keluar
    void wait();                          // tunggu thread selesai (untuk uji)

private:
    void checkWorker(bool pre);
    void downloadWorker();
    void setState(UpdState s, const std::string& err = "");
    void join();

    std::string cur_, url_;
    UpdaterIO io_;
    std::mutex mu_;
    UpdSnapshot snap_;
    std::thread th_;
    std::atomic<bool> cancel_{false};
    std::string file_;
};

}  // namespace ng
