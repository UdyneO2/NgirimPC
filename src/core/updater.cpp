#include "updater.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include "sha256.h"

namespace fs = std::filesystem;

namespace ng {
namespace {

bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}

std::string lowerStr(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// File .sha256 berformat "<hex>" atau "<hex>  NgirimPC.exe"
std::string firstHash(const std::string& txt) {
    size_t i = 0;
    while (i < txt.size() && std::isspace((unsigned char)txt[i])) i++;
    std::string h = lowerStr(txt.substr(i, 64));
    return isHex64(h) ? h : std::string();
}

}  // namespace

Updater::Updater(std::string currentVersion, UpdaterIO io, std::string releasesUrl)
    : cur_(std::move(currentVersion)), url_(std::move(releasesUrl)), io_(std::move(io)) {}

Updater::~Updater() { cancel_ = true; join(); }

void Updater::join() { if (th_.joinable()) th_.join(); }
void Updater::wait() { join(); }
void Updater::cancel() { cancel_ = true; }

void Updater::setState(UpdState s, const std::string& err) {
    std::lock_guard<std::mutex> g(mu_);
    snap_.state = s;
    snap_.error = err;
}

UpdSnapshot Updater::snapshot() {
    std::lock_guard<std::mutex> g(mu_);
    return snap_;
}

void Updater::check(bool pre) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (snap_.state == UpdState::Checking || snap_.state == UpdState::Downloading ||
            snap_.state == UpdState::Verifying || snap_.state == UpdState::Ready) return;
        snap_.state = UpdState::Checking;
        snap_.error.clear();
    }
    join();
    cancel_ = false;
    th_ = std::thread([this, pre] { checkWorker(pre); });
}

void Updater::checkWorker(bool pre) {
    std::string body, err;
    if (!io_.httpGet(url_, body, err)) { setState(UpdState::Failed, err); return; }
    UpdateInfo info = pickRelease(body, cur_, pre);
    if (!info.error.empty()) { setState(UpdState::Failed, info.error); return; }
    std::lock_guard<std::mutex> g(mu_);
    if (info.available) {
        snap_.info = info;
        snap_.state = UpdState::Available;
    } else {
        snap_.state = UpdState::UpToDate;
    }
    snap_.error.clear();
}

void Updater::download() {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (snap_.state != UpdState::Available) return;
        snap_.state = UpdState::Downloading;
        snap_.done = 0;
        snap_.total = snap_.info.size;
        snap_.frac = 0;
    }
    join();
    cancel_ = false;
    th_ = std::thread([this] { downloadWorker(); });
}

void Updater::downloadWorker() {
    UpdateInfo info;
    { std::lock_guard<std::mutex> g(mu_); info = snap_.info; }

    // 1) tentukan hash yang diharapkan SEBELUM mengunduh; tanpa hash, rilis tidak dipasang
    std::string want = lowerStr(info.sha256);
    if (!isHex64(want) && !info.shaUrl.empty()) {
        std::string txt, err;
        if (io_.httpGet(info.shaUrl, txt, err)) want = firstHash(txt);
    }
    if (!isHex64(want)) { setState(UpdState::Failed, "Rilis tidak menyertakan checksum SHA-256, update tidak dipasang"); return; }

    // 2) unduh
    std::string dir = io_.tempDir ? io_.tempDir() : std::string();
    if (dir.empty()) { setState(UpdState::Failed, "Folder sementara tidak tersedia"); return; }
    file_ = dir + "/NgirimPC-" + info.tag + ".exe";
    std::string err;
    bool ok = io_.httpDownload(info.exeUrl, file_,
        [this](int64_t d, int64_t t) {
            std::lock_guard<std::mutex> g(mu_);
            snap_.done = d;
            if (t > 0) snap_.total = t;
            if (snap_.total > 0) snap_.frac = (double)d / (double)snap_.total;
        },
        cancel_, err);
    if (!ok) {
        std::error_code ec;
        fs::remove(fs::u8path(file_), ec);
        if (cancel_) { setState(UpdState::Available); return; }
        setState(UpdState::Failed, err.empty() ? "Unduhan gagal" : err);
        return;
    }

    // 3) verifikasi: ukuran, SHA-256, header PE
    setState(UpdState::Verifying);
    auto fail = [&](const std::string& m) {
        std::error_code ec;
        fs::remove(fs::u8path(file_), ec);
        setState(UpdState::Failed, m);
    };
    std::ifstream f(fs::u8path(file_), std::ios::binary);
    if (!f) { fail("File unduhan tidak bisa dibuka"); return; }
    Sha256 h;
    std::vector<char> buf(1 << 16);
    int64_t total = 0;
    char head[2] = {0, 0};
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n <= 0) break;
        if (total == 0) { head[0] = buf[0]; head[1] = n > 1 ? buf[1] : 0; }
        h.update(buf.data(), (size_t)n);
        total += n;
        if (cancel_) { fail("Dibatalkan"); return; }
    }
    uint8_t dg[32];
    h.final(dg);
    if (info.size > 0 && total != info.size) { fail("Ukuran unduhan tidak sesuai"); return; }
    if (toHex(dg, 32) != want) { fail("Checksum SHA-256 tidak cocok, update dibatalkan demi keamanan"); return; }
    if (head[0] != 'M' || head[1] != 'Z') { fail("File unduhan bukan program Windows"); return; }

    std::lock_guard<std::mutex> g(mu_);
    snap_.done = snap_.total = total;
    snap_.frac = 1.0;
    snap_.state = UpdState::Ready;
    snap_.error.clear();
}

bool Updater::apply(std::string& err) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (snap_.state != UpdState::Ready) { err = "Update belum siap"; return false; }
    }
    if (!io_.applyUpdate) { err = "Pemasangan tidak tersedia"; return false; }
    return io_.applyUpdate(file_, err);
}

}  // namespace ng
