#include "proto.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <thread>
#include <tuple>

#include "sha256.h"

namespace ng {

static const int32_t MAGIC = 0x4E475231;  // "NGR1"
static const int VERSION = 2;
static const int MAX_PIN_FAILS = 5;

struct SecurityError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ------------------------------------------------------------------ util
std::string fmtSize(double b) {
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    while (b >= 1024 && i < 4) { b /= 1024; i++; }
    char buf[48];
    std::snprintf(buf, sizeof buf, i ? "%.2f %s" : "%.0f %s", b, u[i]);
    return buf;
}

std::string fmtEta(double s) {
    if (s < 0 || s > 172800) return "--";
    int t = (int)s;
    char buf[32];
    if (t >= 3600) std::snprintf(buf, sizeof buf, "%dj %02dm", t / 3600, (t % 3600) / 60);
    else std::snprintf(buf, sizeof buf, "%dm %02ds", t / 60, t % 60);
    return buf;
}

static int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Job::emit(const std::string& cur, bool force) {
    int64_t now = nowNs();
    if (!force && now - lastEmit_ < 150000000LL) return;
    if (lastEmit_ != 0) {
        double dt = (double)(now - lastEmit_) / 1e9;
        if (dt > 0) {
            double inst = (double)(done - lastDone_) / dt;
            speed_ = speed_ == 0 ? inst : speed_ * 0.7 + inst * 0.3;
        }
    }
    lastEmit_ = now;
    lastDone_ = done;
    if (cb_.progress) cb_.progress(cur, done, total_, speed_);
}

fs::path uniqueFile(const fs::path& f) {
    std::error_code ec;
    if (!fs::exists(f, ec)) return f;
    std::string stem = U(f.stem()), ext = U(f.extension());
    for (int n = 1;; n++) {
        fs::path c = f.parent_path() / P(stem + " (" + std::to_string(n) + ")" + ext);
        if (!fs::exists(c, ec)) return c;
    }
}

std::string randomPin() {
    std::random_device rd;
    std::uniform_int_distribution<int> d(0, 999999);
    char b[8];
    std::snprintf(b, sizeof b, "%06d", d(rd));
    return b;
}

static void randomBytes(uint8_t* p, size_t n) {
    std::random_device rd;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(rd() & 0xFF);
}

static std::vector<uint8_t> authMac(const std::string& pin, const uint8_t* nonce, int mode, const std::string& peer) {
    std::vector<uint8_t> m(nonce, nonce + 16);
    m.push_back((uint8_t)mode);
    m.insert(m.end(), peer.begin(), peer.end());
    return hmacSha256(pin, m);
}

static void reply(Conn& c, bool ok, const std::string& msg) {
    c.writeByte(ok ? 1 : 0);
    c.writeUTF(msg);
    c.flush();
}

static std::vector<std::string> splitSlash(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// Nama file Windows: ganti karakter terlarang, buang titik/spasi di ujung, hindari nama perangkat.
static std::string sanitizeComponent(std::string n) {
#ifdef _WIN32
    for (auto& ch : n)
        if ((unsigned char)ch < 32 || std::strchr("<>:\"|?*", ch)) ch = '_';
    while (!n.empty() && (n.back() == '.' || n.back() == ' ')) n.pop_back();
    if (n.empty()) n = "_";
    std::string up = n.substr(0, n.find('.'));
    for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
    static const char* bad[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* b : bad) if (up == b) { n = "_" + n; break; }
#endif
    return n;
}

static fs::path safeResolve(const fs::path& root, const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel.find('\\') != std::string::npos) throw SecurityError("path ilegal: " + rel);
    fs::path f = root;
    for (const auto& comp : splitSlash(rel)) {
        if (comp.empty() || comp == "." || comp == "..") throw SecurityError("path ilegal: " + rel);
        f /= P(sanitizeComponent(comp));
    }
    std::error_code ec;
    fs::path rc = fs::weakly_canonical(root, ec);
    fs::path fc = fs::weakly_canonical(f, ec);
    fs::path r = fc.lexically_relative(rc);
    if (r.empty() || *r.begin() == "..") throw SecurityError("path keluar folder: " + rel);
    return f;
}

// ------------------------------------------------------------------ Engine: dasar
Engine::Engine(Callbacks cb) : cb_(std::move(cb)) { netInit(); }

Engine::~Engine() {
    stopListen();
    cancel();
    for (int i = 0; i < 60 && handlers_.load() > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void Engine::addActive(sock_t s) { std::lock_guard<std::mutex> l(activeMu_); active_.insert(s); }
void Engine::delActive(sock_t s) { std::lock_guard<std::mutex> l(activeMu_); active_.erase(s); }

void Engine::closeActive() {
    std::lock_guard<std::mutex> l(activeMu_);
    for (sock_t s : active_) sockShutdown(s);  // pemilik soket yang menutup
}

void Engine::cancel() {
    cancelled_ = true;
    closeActive();
}

void Engine::setAllowBrowse(bool allow) { allowBrowse_ = allow; }

void Engine::setShareRoot(const fs::path& root) {
    std::lock_guard<std::mutex> l(mu_);
    share_ = root;
}

bool Engine::listening() const {
    std::lock_guard<std::mutex> l(mu_);
    return listener_ != NG_BAD_SOCK;
}

// ------------------------------------------------------------------ server
int Engine::listen(const fs::path& receiveDir, const std::string& pin, const std::string& deviceName) {
    stopListen();
    try {
        std::error_code ec;
        fs::create_directories(receiveDir, ec);
        if (!fs::is_directory(receiveDir, ec)) throw NetError("Folder tujuan tidak valid");
        int port = 0;
        sock_t s = netListen(port);
        {
            std::lock_guard<std::mutex> l(mu_);
            listener_ = s;
            root_ = receiveDir;
            pin_ = pin;
            name_ = deviceName;
        }
        pinFails_ = 0;
        handlers_++;
        std::thread([this, s, port] {
            log("Server terima aktif di port " + std::to_string(port));
            acceptLoop(s);
            log("Server terima berhenti");
            handlers_--;
        }).detach();
        return port;
    } catch (const std::exception& e) {
        log(std::string("ERR listen: ") + e.what());
        return -1;
    }
}

void Engine::stopListen() {
    sock_t s;
    {
        std::lock_guard<std::mutex> l(mu_);
        s = listener_;
        listener_ = NG_BAD_SOCK;
    }
    if (s != NG_BAD_SOCK) {
        sockShutdown(s);
        sockClose(s);
    }
    closeActive();
}

void Engine::acceptLoop(sock_t ss) {
    while (true) {
        std::string ip;
        sock_t c = netAccept(ss, ip);
        if (c == NG_BAD_SOCK) break;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (listener_ != ss) { sockClose(c); break; }
        }
        addActive(c);
        handlers_++;
        std::thread([this, c, ip] {
            try { incoming(c, ip); } catch (...) {}
            delActive(c);
            handlers_--;
        }).detach();
    }
}

void Engine::incoming(sock_t s, const std::string& from) {
    Conn c(s);  // memiliki soket; ditutup saat keluar
    c.setTimeoutMs(60000);
    fs::path root;
    std::string pin, name;
    {
        std::lock_guard<std::mutex> l(mu_);
        root = root_; pin = pin_; name = name_;
    }
    uint8_t nonce[16];
    randomBytes(nonce, 16);
    try {
        c.writeInt(MAGIC); c.writeByte(VERSION); c.write(nonce, 16); c.writeUTF(name); c.flush();
    } catch (...) { return; }

    // Koneksi yang diputus sebelum autentikasi (cek "perangkat aktif") diabaikan diam-diam
    int mode;
    std::string peer;
    uint8_t mac[32];
    try {
        mode = c.readByte();
        peer = c.readUTF();
        c.readFully(mac, 32);
    } catch (...) { return; }

    std::string shown = peer.size() > 64 ? peer.substr(0, 64) : peer;
    std::vector<uint8_t> want = authMac(pin, nonce, mode, peer);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(want[i] ^ mac[i]);
    if (diff != 0) {
        reply(c, false, "PIN salah");
        log("ERR PIN salah dari " + shown + " (" + from + ")");
        if (++pinFails_ >= MAX_PIN_FAILS) {
            stopListen();
            finish(false, "Server dihentikan: terlalu banyak PIN salah", 0, 0);
        }
        return;
    }
    pinFails_ = 0;
    reply(c, true, "OK");

    switch (mode) {
    case 1:
        log("OK     pairing dengan " + shown + " (" + from + ")");
        break;
    case 2:
        log("Pengirim terhubung: " + shown + " (" + from + ")");
        serverTransfer(c, [&] { return receiveSession(c, root, shown); });
        break;
    case 3:
        try { serveList(c); }
        catch (const std::exception& e) { log("ERR jelajah dari " + shown + ": " + e.what()); }
        break;
    case 4: {
        std::vector<std::string> paths;
        try {
            int n = c.readInt();
            if (n < 0 || n > 10000) return;
            for (int i = 0; i < n; i++) paths.push_back(c.readUTF());
        } catch (...) { return; }
        if (!allowBrowse_) { reply(c, false, "Akses jelajah dimatikan di perangkat penerima"); return; }
        std::vector<fs::path> files;
        try { for (auto& p : paths) files.push_back(resolveRemote(p)); }
        catch (const SecurityError& e) { reply(c, false, "Path ditolak"); log(std::string("ERR ") + e.what()); return; }
        reply(c, true, "OK");
        log(shown + " mengambil " + std::to_string(files.size()) + " item");
        serverTransfer(c, [&] { return sendSession(c, files, shown); });
        break;
    }
    case 5: {
        std::string destStr;
        try { destStr = c.readUTF(); } catch (...) { return; }
        if (!allowBrowse_) { reply(c, false, "Akses jelajah dimatikan di perangkat penerima"); return; }
        fs::path dest;
        try { dest = resolveRemote(destStr); }
        catch (const SecurityError& e) { reply(c, false, "Path ditolak"); log(std::string("ERR ") + e.what()); return; }
        std::error_code ec;
        fs::create_directories(dest, ec);
        if (!fs::is_directory(dest, ec)) { reply(c, false, "Folder tujuan tidak valid"); return; }
        reply(c, true, "OK");
        log(shown + " mengirim ke " + U(dest));
        serverTransfer(c, [&] { return receiveSession(c, dest, shown); });
        break;
    }
    default:
        break;
    }
}

void Engine::serverTransfer(Conn& c, const std::function<Outcome()>& block) {
    std::unique_lock<std::mutex> lk(transferMu_, std::try_to_lock);
    if (!lk.owns_lock()) {
        try { reply(c, false, "Perangkat sedang sibuk"); } catch (...) {}
        return;
    }
    cancelled_ = false;
    try {
        Outcome r = block();
        finish(r.ok, r.msg, r.done, r.total);
    } catch (const std::exception& e) {
        if (cancelled_) { log("ERR sesi dibatalkan"); finish(false, "Sesi dibatalkan", 0, 0); }
        else { log(std::string("ERR sesi: ") + e.what()); finish(false, std::string("Sesi gagal: ") + e.what(), 0, 0); }
    }
}

fs::path Engine::resolveRemote(const std::string& p) {
    if (p.empty() || p[0] != '/') throw SecurityError("path harus absolut: " + p);
    std::string rest;
    if (p == "/sdcard") rest = "";
    else if (p.rfind("/sdcard/", 0) == 0) rest = p.substr(8);
    else throw SecurityError("akses di luar /sdcard ditolak: " + p);
    fs::path root;
    {
        std::lock_guard<std::mutex> l(mu_);
        root = share_;
    }
    if (root.empty()) throw SecurityError("folder berbagi belum diatur");
    fs::path f = root;
    for (const auto& comp : splitSlash(rest)) {
        if (comp.empty()) continue;
        if (comp == "." || comp == ".." || comp.find('\\') != std::string::npos || comp.find(':') != std::string::npos)
            throw SecurityError("path ilegal: " + p);
        f /= P(comp);
    }
    std::error_code ec;
    fs::path rc = fs::weakly_canonical(root, ec);
    fs::path fc = fs::weakly_canonical(f, ec);
    fs::path r = fc.lexically_relative(rc);
    if (r.empty() || *r.begin() == "..") throw SecurityError("akses di luar folder berbagi ditolak: " + p);
    return fc;
}

void Engine::serveList(Conn& c) {
    std::string path = c.readUTF();
    if (!allowBrowse_) { reply(c, false, "Akses jelajah dimatikan di perangkat penerima"); return; }
    fs::path dir;
    try { dir = resolveRemote(path); } catch (const SecurityError&) { reply(c, false, "Path ditolak"); return; }
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { reply(c, false, "Bukan folder"); return; }
    struct K { std::string name; int64_t size; bool dir; };
    std::vector<K> kids;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        bool d = it->is_directory(e2);
        int64_t sz = 0;
        if (!d) { auto n = it->file_size(e2); sz = e2 ? 0 : (int64_t)n; }
        kids.push_back({U(it->path().filename()), sz, d});
    }
    auto low = [](std::string s) { for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };
    std::sort(kids.begin(), kids.end(), [&](const K& a, const K& b) {
        if (a.dir != b.dir) return a.dir;
        return low(a.name) < low(b.name);
    });
    reply(c, true, "OK");
    c.writeInt((int32_t)kids.size());
    for (auto& k : kids) { c.writeUTF(k.name); c.writeLong(k.size); c.writeByte(k.dir ? 1 : 0); }
    c.flush();
}

// ------------------------------------------------------------------ sesi isi file
Engine::Outcome Engine::receiveSession(Conn& c, const fs::path& root, const std::string& label) {
    int32_t count = c.readInt();
    int64_t total = c.readLong();
    if (count < 0 || count > 2000000 || total < 0) throw NetError("Manifest tidak valid");
    std::vector<Ent> list;
    list.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        Ent e;
        e.rel = c.readUTF();
        e.dir = c.readByte() == 1;
        e.size = c.readLong();
        list.push_back(std::move(e));
    }
    std::vector<fs::path> targets;
    try {
        for (auto& e : list) targets.push_back(safeResolve(root, e.rel));
    } catch (const SecurityError& e) {
        reply(c, false, "Path ditolak");
        return {false, std::string("Path ditolak: ") + e.what(), 0, total};
    }
    std::error_code ec;
    auto sp = fs::space(root, ec);
    if (!ec && (int64_t)sp.available < total) {
        reply(c, false, "Ruang penyimpanan tidak cukup");
        return {false, "Ruang tidak cukup (butuh " + fmtSize((double)total) + ")", 0, total};
    }
    reply(c, true, "SIAP");
    log("Menerima " + std::to_string(list.size()) + " entri (" + fmtSize((double)total) + ")");

    Job job(cb_, total);
    int files = 0;
    for (size_t i = 0; i < list.size(); i++) {
        const Ent& e = list[i];
        const fs::path& target = targets[i];
        if (e.dir) { fs::create_directories(target, ec); continue; }
        fs::create_directories(target.parent_path(), ec);
        fs::path dst = uniqueFile(target);
        bool okFile = false;
        try {
            {
                std::ofstream o(dst, std::ios::binary | std::ios::trunc);
                if (!o) throw NetError("Tidak bisa membuat file: " + U(dst.filename()));
                int64_t left = e.size;
                while (left > 0) {
                    size_t want = (size_t)std::min<int64_t>(left, (int64_t)job.buf.size());
                    size_t n = c.readSome(job.buf.data(), want);
                    o.write(job.buf.data(), (std::streamsize)n);
                    if (!o) throw NetError("Gagal menulis ke disk (penuh?)");
                    left -= (int64_t)n;
                    job.done += (int64_t)n;
                    job.emit(e.rel);
                    if (cancelled_) throw Cancelled();
                }
            }
            okFile = true;
        } catch (...) {
            if (!okFile) { std::error_code e3; fs::remove(dst, e3); }
            throw;
        }
        files++;
        log("OK     " + e.rel);
    }
    job.emit("", true);
    c.writeByte(1);
    c.flush();
    return {true, "Selesai: " + std::to_string(files) + " file diterima dari " + label + " (" + fmtSize((double)job.done) + ")",
            job.done, total};
}

static void walkManifest(const std::string& rel, const fs::path& f, std::vector<std::tuple<std::string, fs::path, bool, int64_t>>& out) {
    std::error_code ec;
    fs::file_status sst = fs::symlink_status(f, ec);
    if (ec) return;
    if (fs::is_symlink(sst)) {  // symlink ke file diikuti; ke folder dilewati (hindari siklus)
        if (fs::is_regular_file(f, ec)) out.emplace_back(rel, f, false, (int64_t)fs::file_size(f, ec));
        return;
    }
    if (fs::is_directory(sst)) {
        out.emplace_back(rel, f, true, 0);
        std::vector<fs::path> kids;
        for (fs::directory_iterator it(f, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            kids.push_back(it->path());
        std::sort(kids.begin(), kids.end(), [](const fs::path& a, const fs::path& b) { return U(a.filename()) < U(b.filename()); });
        for (auto& k : kids) walkManifest(rel + "/" + U(k.filename()), k, out);
    } else if (fs::is_regular_file(sst)) {
        out.emplace_back(rel, f, false, (int64_t)fs::file_size(f, ec));
    }
}

Engine::Outcome Engine::sendSession(Conn& c, const std::vector<fs::path>& srcs, const std::string& label) {
    std::vector<std::tuple<std::string, fs::path, bool, int64_t>> raw;
    for (auto& s : srcs) walkManifest(U(s.filename()), s, raw);
    std::vector<SendEnt> list;
    int64_t total = 0;
    int fileCount = 0;
    for (auto& t : raw) {
        list.push_back({std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t)});
        total += std::get<3>(t);
        if (!std::get<2>(t)) fileCount++;
    }
    log("Menyiapkan " + std::to_string(list.size()) + " entri (" + std::to_string(fileCount) + " file, " +
        fmtSize((double)total) + ")");

    c.writeInt((int32_t)list.size());
    c.writeLong(total);
    for (auto& e : list) { c.writeUTF(e.rel); c.writeByte(e.dir ? 1 : 0); c.writeLong(e.size); }
    c.flush();
    if (c.readByte() != 1) throw NetError("Ditolak penerima: " + c.readUTF());
    c.readUTF();

    Job job(cb_, total);
    for (auto& e : list) {
        if (e.dir) continue;
        std::ifstream in(e.f, std::ios::binary);
        if (!in) throw NetError("Tidak bisa membuka: " + e.rel);
        int64_t left = e.size;
        while (left > 0) {
            size_t want = (size_t)std::min<int64_t>(left, (int64_t)job.buf.size());
            in.read(job.buf.data(), (std::streamsize)want);
            std::streamsize n = in.gcount();
            if (n <= 0) throw NetError("File berubah saat dikirim: " + e.rel);
            c.write(job.buf.data(), (size_t)n);
            left -= n;
            job.done += n;
            job.emit(e.rel);
            if (cancelled_) throw Cancelled();
        }
        log("OK     " + e.rel);
    }
    c.flush();
    if (c.readByte() != 1) throw NetError("Penerima gagal menyimpan");
    job.emit("", true);
    return {true, "Selesai: " + std::to_string(fileCount) + " file terkirim ke " + label + " (" + fmtSize((double)job.done) + ")",
            job.done, total};
}

// ------------------------------------------------------------------ klien
std::string Engine::handshake(Conn& c, int mode, const std::string& pin, const std::string& name) {
    if (c.readInt() != MAGIC) throw NetError("Bukan server Ngirim");
    if (c.readByte() != VERSION) throw NetError("Versi protokol berbeda - perbarui Ngirim di kedua perangkat");
    uint8_t nonce[16];
    c.readFully(nonce, 16);
    std::string serverName = c.readUTF();
    c.writeByte(mode);
    c.writeUTF(name);
    std::vector<uint8_t> mac = authMac(pin, nonce, mode, name);
    c.write(mac.data(), mac.size());
    c.flush();
    if (c.readByte() != 1) throw NetError(c.readUTF());
    c.readUTF();
    return serverName;
}

void Engine::clientJob(const std::string& host, int port, int mode, const std::string& pin, const std::string& name,
                       const std::function<void(Conn&, const std::string&)>& body) {
    sock_t s = netConnect(host, port, mode == 1 ? 5000 : 8000);
    addActive(s);
    Conn c(s);
    struct Guard { Engine* e; sock_t s; ~Guard() { e->delActive(s); } } g{this, s};
    c.setTimeoutMs(mode == 1 ? 8000 : (mode == 3 ? 15000 : 60000));
    std::string peer = handshake(c, mode, pin, name);
    body(c, peer);
}

std::string Engine::checkPair(const std::string& host, int port, const std::string& pin, const std::string& name) {
    try {
        clientJob(host, port, 1, pin, name, [](Conn&, const std::string&) {});
        return "";
    } catch (const std::exception& e) {
        return e.what();
    }
}

bool Engine::probe(const std::string& host, int port, int timeoutMs) {
    try {
        sock_t s = netConnect(host, port, timeoutMs);
        Conn c(s);
        c.setTimeoutMs(timeoutMs);
        return c.readInt() == MAGIC;
    } catch (...) {
        return false;
    }
}

std::vector<RemoteEntry> Engine::remoteList(const std::string& host, int port, const std::string& pin,
                                            const std::string& name, const std::string& path, std::string& err) {
    std::vector<RemoteEntry> res;
    err.clear();
    try {
        clientJob(host, port, 3, pin, name, [&](Conn& c, const std::string&) {
            c.writeUTF(path);
            c.flush();
            if (c.readByte() != 1) { err = c.readUTF(); return; }
            c.readUTF();
            int n = c.readInt();
            if (n < 0 || n > 200000) throw NetError("Daftar tidak valid");
            res.reserve((size_t)n);
            for (int i = 0; i < n; i++) {
                RemoteEntry e;
                e.name = c.readUTF();
                e.size = c.readLong();
                e.dir = c.readByte() == 1;
                res.push_back(std::move(e));
            }
        });
    } catch (const std::exception& e) {
        err = e.what();
        res.clear();
    }
    return res;
}

void Engine::startClient(std::function<Outcome()> block) {
    if (running_.exchange(true)) { finish(false, "Transfer lain masih berjalan", 0, 0); return; }
    cancelled_ = false;
    handlers_++;
    std::thread([this, block] {
        Outcome r{false, "", 0, 0};
        try { r = block(); }
        catch (const std::exception& e) {
            if (cancelled_) { log("ERR dibatalkan"); r = {false, "Dibatalkan", 0, 0}; }
            else { log(std::string("ERR ") + e.what()); r = {false, std::string("Gagal: ") + e.what(), 0, 0}; }
        }
        running_ = false;  // dibersihkan sebelum callback agar UI boleh langsung memulai operasi berikutnya
        finish(r.ok, r.msg, r.done, r.total);
        handlers_--;
    }).detach();
}

void Engine::send(const std::string& host, int port, const std::string& pin, const std::string& name,
                  const std::vector<fs::path>& srcs) {
    startClient([=]() -> Outcome {
        Outcome r{false, "", 0, 0};
        clientJob(host, port, 2, pin, name, [&](Conn& c, const std::string& peer) {
            log("OK     terautentikasi ke " + peer);
            r = sendSession(c, srcs, peer);
        });
        return r;
    });
}

void Engine::push(const std::string& host, int port, const std::string& pin, const std::string& name,
                  const std::vector<fs::path>& srcs, const std::string& remoteDir) {
    startClient([=]() -> Outcome {
        Outcome r{false, "", 0, 0};
        clientJob(host, port, 5, pin, name, [&](Conn& c, const std::string& peer) {
            log("OK     terautentikasi ke " + peer);
            c.writeUTF(remoteDir);
            c.flush();
            if (c.readByte() != 1) throw NetError("Ditolak penerima: " + c.readUTF());
            c.readUTF();
            r = sendSession(c, srcs, peer);
        });
        return r;
    });
}

void Engine::pull(const std::string& host, int port, const std::string& pin, const std::string& name,
                  const std::vector<std::string>& remotePaths, const fs::path& destDir) {
    startClient([=]() -> Outcome {
        Outcome r{false, "", 0, 0};
        clientJob(host, port, 4, pin, name, [&](Conn& c, const std::string& peer) {
            log("OK     terautentikasi ke " + peer);
            c.writeInt((int32_t)remotePaths.size());
            for (auto& p : remotePaths) c.writeUTF(p);
            c.flush();
            if (c.readByte() != 1) throw NetError("Ditolak: " + c.readUTF());
            c.readUTF();
            std::error_code ec;
            fs::create_directories(destDir, ec);
            if (!fs::is_directory(destDir, ec)) throw NetError("Folder tujuan tidak valid");
            r = receiveSession(c, destDir, peer);
        });
        return r;
    });
}

}  // namespace ng
