// Protokol jaringan Ngirim v2 (kompatibel dengan aplikasi Android Ngirim v1.2-beta ke atas).
// Server/klien TCP: pairing PIN (HMAC-SHA256), kirim, terima, jelajah folder, ambil, dorong.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "netio.h"

namespace ng {

namespace fs = std::filesystem;
inline fs::path P(const std::string& u8) { return fs::u8path(u8); }
inline std::string U(const fs::path& p) { return p.u8string(); }

std::string fmtSize(double bytes);
std::string fmtEta(double seconds);

struct Callbacks {
    std::function<void(const std::string&)> log;
    std::function<void(const std::string& cur, int64_t done, int64_t total, double speed)> progress;
    std::function<void(bool ok, const std::string& msg, int64_t done, int64_t total)> finish;
};

struct RemoteEntry {
    std::string name;
    int64_t size = 0;
    bool dir = false;
};

struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("dibatalkan") {}
};

/** Penolakan dari lawan bicara (PIN salah, sibuk, path ditolak): bukan gangguan jaringan. */
struct Rejected : NetError {
    using NetError::NetError;
};

/** Penghitung progres + kecepatan (rata-rata bergerak), dibatasi ~6 pembaruan/detik. */
class Job {
public:
    Job(const Callbacks& cb, int64_t total) : buf(1 << 20), cb_(cb), total_(total) {}
    int64_t done = 0;
    std::vector<char> buf;
    void emit(const std::string& cur, bool force = false);
    /** Byte yang sudah ada di penerima (lanjutan): hitung sebagai selesai tanpa memengaruhi kecepatan. */
    void skip(int64_t n) { done += n; lastDone_ = done; }

private:
    const Callbacks& cb_;
    int64_t total_;
    int64_t lastEmit_ = 0, lastDone_ = 0;
    double speed_ = 0;
};

fs::path uniqueFile(const fs::path& f);

/** Waktu ubah file dalam milidetik sejak epoch Unix (sama dengan File.lastModified() di Android). */
int64_t fileMtimeMs(const fs::path& f);
/** Nama file sementara penerima untuk transfer yang bisa dilanjutkan: <nama>.<ukuran>-<mtime>.ngpart */
fs::path partFilePath(const fs::path& target, int64_t size, int64_t mtimeMs);
std::string randomPin();  // 6 digit

class Engine {
public:
    explicit Engine(Callbacks cb);
    ~Engine();

    // ---- server (penerima) ----
    int listen(const fs::path& receiveDir, const std::string& pin, const std::string& deviceName);  // port, atau -1
    void stopListen();
    bool listening() const;
    void setAllowBrowse(bool allow);
    void setShareRoot(const fs::path& root);  // "/sdcard" dari perangkat lain dipetakan ke folder ini

    // ---- klien ----
    std::string checkPair(const std::string& host, int port, const std::string& pin, const std::string& name);  // "" = ok
    bool probe(const std::string& host, int port, int timeoutMs);  // apakah ada server Ngirim di sana
    std::vector<RemoteEntry> remoteList(const std::string& host, int port, const std::string& pin,
                                        const std::string& name, const std::string& path, std::string& err);
    void send(const std::string& host, int port, const std::string& pin, const std::string& name,
              const std::vector<fs::path>& srcs);  // ke folder terima
    void push(const std::string& host, int port, const std::string& pin, const std::string& name,
              const std::vector<fs::path>& srcs, const std::string& remoteDir);
    void pull(const std::string& host, int port, const std::string& pin, const std::string& name,
              const std::vector<std::string>& remotePaths, const fs::path& destDir);

    void cancel();
    bool busy() const { return running_.load(); }
    /** Dipanggil tepat sekali tiap pekerjaan klien (kirim/unggah/unduh) selesai; dipakai antrean multi-perangkat. */
    void setClientDone(std::function<void(bool ok, const std::string& msg)> f) { clientDone_ = std::move(f); }

private:
    struct Ent { std::string rel; bool dir; int64_t size; int64_t mtime; };
    struct SendEnt { std::string rel; fs::path f; bool dir; int64_t size; int64_t mtime; };
    struct Outcome { bool ok; std::string msg; int64_t done; int64_t total; };

    Callbacks cb_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> allowBrowse_{false};
    std::atomic<int> pinFails_{0};
    std::atomic<int> handlers_{0};
    mutable std::mutex mu_;
    sock_t listener_ = NG_BAD_SOCK;
    fs::path root_, share_;
    std::string pin_, name_;
    std::mutex activeMu_;
    std::set<sock_t> active_;
    std::mutex transferMu_;

    void log(const std::string& m) { if (cb_.log) cb_.log(m); }
    void finish(bool ok, const std::string& m, int64_t d, int64_t t) { if (cb_.finish) cb_.finish(ok, m, d, t); }
    void addActive(sock_t s);
    void delActive(sock_t s);
    void closeActive();
    void acceptLoop(sock_t ss);
    void incoming(sock_t s, const std::string& peerIp);
    void serverTransfer(Conn& c, const std::function<Outcome()>& block);
    void serveList(Conn& c);
    Outcome receiveSession(Conn& c, const fs::path& root, const std::string& label);
    Outcome sendSession(Conn& c, const std::vector<fs::path>& srcs, const std::string& label);
    std::string handshake(Conn& c, int mode, const std::string& pin, const std::string& name);
    fs::path resolveRemote(const std::string& p);
    void startClient(std::function<Outcome()> block);
    std::function<void(bool, const std::string&)> clientDone_;
    Outcome withResume(const std::function<Outcome()>& session);
    void clientJob(const std::string& host, int port, int mode, const std::string& pin, const std::string& name,
                   const std::function<void(Conn&, const std::string&)>& body);
};

}  // namespace ng
