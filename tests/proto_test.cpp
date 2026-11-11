// Uji inti protokol: vektor kripto, UTF-8 ala Java, loopback C++<->C++, dan (opsional) interop dengan RefPeer.java.
// Pemakaian: ng_test [folder-berisi-RefPeer.java]
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include "../src/core/proto.h"
#include "../src/core/sha256.h"
#include "../src/core/mdns.h"
#include "../src/core/updater_core.h"


#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

using namespace ng;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) { std::printf("  GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } \
    } while (0)

struct Sink {
    std::mutex m;
    std::condition_variable cv;
    bool done = false, ok = false;
    std::string msg;
    std::vector<std::string> logs;
    void reset() { std::lock_guard<std::mutex> l(m); done = false; ok = false; msg.clear(); }
    bool wait(int ms = 20000) {
        std::unique_lock<std::mutex> l(m);
        return cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return done; });
    }
    Callbacks cbs() {
        Callbacks c;
        c.log = [this](const std::string& s) { std::lock_guard<std::mutex> l(m); logs.push_back(s); };
        c.progress = [](const std::string&, int64_t, int64_t, double) {};
        c.finish = [this](bool o, const std::string& s, int64_t, int64_t) {
            std::lock_guard<std::mutex> l(m); ok = o; msg = s; done = true; cv.notify_all();
        };
        return c;
    }
};

static std::string slurp(const fs::path& p) {
    std::ifstream i(p, std::ios::binary);
    std::stringstream ss; ss << i.rdbuf();
    return ss.str();
}

static bool sameTree(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::is_directory(a) != fs::is_directory(b)) return false;
    if (!fs::is_directory(a)) return slurp(a) == slurp(b);
    std::vector<std::string> na, nb;
    for (auto& e : fs::directory_iterator(a)) na.push_back(U(e.path().filename()));
    for (auto& e : fs::directory_iterator(b)) nb.push_back(U(e.path().filename()));
    std::sort(na.begin(), na.end()); std::sort(nb.begin(), nb.end());
    if (na != nb) return false;
    for (auto& n : na) if (!sameTree(a / P(n), b / P(n))) return false;
    return true;
}

static void writeFile(const fs::path& p, size_t n, unsigned seed) {
    fs::create_directories(p.parent_path());
    std::mt19937 g(seed);
    std::ofstream o(p, std::ios::binary);
    for (size_t i = 0; i < n; i++) { char c = (char)(g() & 0xFF); o.write(&c, 1); }
}

static std::string run(const std::string& cmd) {
    std::string out;
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    if (std::getenv("NG_DEBUG")) std::printf("--- %s\n%s\n", cmd.c_str(), out.c_str());
    return out;
}

static std::string q(const std::string& s) { return "'" + s + "'"; }
static bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

int main(int argc, char** argv) {
    std::string javaDir = argc > 1 ? argv[1] : "";
    fs::path T = fs::temp_directory_path() / ("ngtest_" + std::to_string(std::random_device{}()));
    fs::create_directories(T);

    std::puts("[1] SHA-256 / HMAC / UTF-8");
    {
        Sha256 s; s.update("abc", 3); uint8_t d[32]; s.final(d);
        CHECK(toHex(d, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        std::string key = "Jefe", m = "what do ya want for nothing?";
        auto h = hmacSha256(key, std::vector<uint8_t>(m.begin(), m.end()));
        CHECK(toHex(h.data(), 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
        std::string u = std::string("a") + '\0' + "b\xF0\x9F\x98\x80\xC3\xA9\xE4\xB8\xAD";  // a NUL b U+1F600 e-acute U+4E2D
        std::string m8 = toModifiedUtf8(u);
        CHECK(m8.find('\0') == std::string::npos);                    // NUL jadi C0 80
        CHECK(m8.size() == 1 + 2 + 1 + 6 + 2 + 3);                    // pasangan surrogat 2x3 byte
        CHECK(fromModifiedUtf8(m8) == u);
    }

    std::puts("[2] updater: versi + JSON");
    {
        CHECK(compareVersions("2.0.1", "2.0.0") > 0);
        CHECK(compareVersions("v2.1", "2.0.9") > 0);
        CHECK(compareVersions("2.0", "2.0.0") == 0);
        CHECK(compareVersions("2.0.0-beta", "2.0.0") < 0);
        CHECK(compareVersions("v10.0.0", "9.9.9") > 0);
        const char* js =
            "[{\"tag_name\":\"v2.1.0\",\"draft\":false,\"prerelease\":false,\"body\":\"Baru\\n- Satu \\u00e9\",\"assets\":["
            "{\"name\":\"NgirimPC.exe\",\"browser_download_url\":\"https://github.com/UdyneO2/NgirimPC/releases/download/v2.1.0/NgirimPC.exe\",\"size\":123},"
            "{\"name\":\"NgirimPC.exe.sha256\",\"browser_download_url\":\"https://github.com/x/y/z.sha256\"}]},"
            "{\"tag_name\":\"v2.2.0-beta\",\"draft\":false,\"prerelease\":true,\"body\":\"\",\"assets\":[{\"name\":\"NgirimPC.exe\",\"browser_download_url\":\"https://github.com/b/NgirimPC.exe\"}]},"
            "{\"tag_name\":\"v2.0.0\",\"draft\":false,\"prerelease\":false,\"body\":\"\",\"assets\":[]}]";
        UpdateInfo u = pickRelease(js, "2.0.0", false);
        CHECK(u.available && u.tag == "v2.1.0" && has(u.exeUrl, "NgirimPC.exe") && has(u.shaUrl, ".sha256"));
        CHECK(has(u.notes, "Satu \xC3\xA9"));
        CHECK(!pickRelease(js, "2.1.0", false).available);
        CHECK(pickRelease(js, "2.0.0", true).tag == "v2.2.0-beta");
    }

    std::puts("[2b] mDNS: bangun + urai paket");
    {
        auto pk = buildAnnounce("Pixel \xC3\xA9", "ngirim-pixel", 40123, "192.168.1.7", 120);
        DnsMsg m;
        CHECK(parseDns(pk.data(), pk.size(), m) && m.response && m.records.size() == 4);
        bool ptr = false, srv = false, a = false;
        for (auto& r : m.records) {
            if (r.type == 12 && r.target.size() == 4 && r.target[0] == "Pixel \xC3\xA9") ptr = true;
            if (r.type == 33 && r.port == 40123 && r.target.size() == 2 && r.target[0] == "ngirim-pixel") srv = true;
            if (r.type == 1 && r.ipv4 == "192.168.1.7") a = true;
        }
        CHECK(ptr && srv && a);
        auto q = buildQuery();
        DnsMsg qm;
        CHECK(parseDns(q.data(), q.size(), qm) && !qm.response && qm.questions.size() == 1 && qm.questions[0].second == 12);
        // paket dengan pointer kompresi nama (seperti yang dikirim Android)
        std::vector<uint8_t> c = {0,0,0x84,0,0,0,0,1,0,0,0,0,
            7,'_','n','g','i','r','i','m',4,'_','t','c','p',5,'l','o','c','a','l',0, 0,12,0,1, 0,0,0,120, 0,6,
            3,'P','i','x',0xC0,12};
        DnsMsg cm;
        CHECK(parseDns(c.data(), c.size(), cm) && cm.records.size() == 1 && cm.records[0].target.size() == 4 && cm.records[0].target[0] == "Pix");
        std::vector<uint8_t> bad = {0,0,0x84,0,0,0,0,1,0,0,0,0, 0xC0,12};  // pointer ke diri sendiri
        DnsMsg bm;
        CHECK(!parseDns(bad.data(), bad.size(), bm));
    }

    std::puts("[3] loopback C++ <-> C++");
    Sink sr, ss;
    Engine R(sr.cbs()), S(ss.cbs());
    fs::path src = T / "src", recv = T / "recv", share = T / "share";
    writeFile(src / "dir1" / "sub" / "file.bin", 3 * 1024 * 1024 + 17, 1);
    writeFile(src / "dir1" / "a.txt", 100, 2);
    fs::create_directories(src / "dir1" / "empty");
    writeFile(src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt"), 5000, 3);
    fs::create_directories(share / "Download");
    writeFile(share / "Download" / "dl.txt", 321, 4);
    R.setShareRoot(share);
    int port = R.listen(recv, "123456", "Rcv-\xC3\xA9");
    CHECK(port > 0);
    CHECK(S.checkPair("127.0.0.1", port, "123456", "Snd-\xF0\x9F\x98\x80").empty());
    CHECK(has(S.checkPair("127.0.0.1", port, "000000", "Snd"), "PIN salah"));
    CHECK(S.probe("127.0.0.1", port, 1000));

    ss.reset();
    S.send("127.0.0.1", port, "123456", "Snd", {src / "dir1", src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt")});
    CHECK(ss.wait()); CHECK(ss.ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(sameTree(src / "dir1", recv / "dir1"));
    CHECK(sameTree(src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt"), recv / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt")));
    CHECK(fs::is_directory(recv / "dir1" / "empty"));

    ss.reset();   // kirim lagi: file bernama sama tidak menimpa -> " (1)"
    S.send("127.0.0.1", port, "123456", "Snd", {src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt")});
    CHECK(ss.wait()); CHECK(ss.ok);
    CHECK(fs::exists(recv / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD (1).txt")));

    std::string err;
    S.remoteList("127.0.0.1", port, "123456", "Snd", "/sdcard", err);
    CHECK(has(err, "dimatikan"));
    R.setAllowBrowse(true);
    auto l = S.remoteList("127.0.0.1", port, "123456", "Snd", "/sdcard", err);
    CHECK(err.empty() && l.size() == 1 && l[0].name == "Download" && l[0].dir);
    l = S.remoteList("127.0.0.1", port, "123456", "Snd", "/sdcard/Download", err);
    CHECK(err.empty() && l.size() == 1 && l[0].size == 321);
    S.remoteList("127.0.0.1", port, "123456", "Snd", "/sdcard/../x", err);  CHECK(has(err, "ditolak"));
    S.remoteList("127.0.0.1", port, "123456", "Snd", "/etc", err);          CHECK(has(err, "ditolak"));

    ss.reset();
    S.pull("127.0.0.1", port, "123456", "Snd", {"/sdcard/Download"}, T / "pulled");
    CHECK(ss.wait()); CHECK(ss.ok);
    CHECK(sameTree(share / "Download", T / "pulled" / "Download"));
    ss.reset();
    S.push("127.0.0.1", port, "123456", "Snd", {src / "dir1"}, "/sdcard/up");
    CHECK(ss.wait()); CHECK(ss.ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(sameTree(src / "dir1", share / "up" / "dir1"));

    if (!javaDir.empty()) {
        std::puts("[4] interop: klien Java asli -> server C++");
        std::string J = "LC_ALL=C.UTF-8 java -Dfile.encoding=UTF-8 -Dstdout.encoding=UTF-8 " + javaDir + "/RefPeer.java ";
        std::string p = std::to_string(port);
        std::string o = run(J + "ping 127.0.0.1 " + p + " 123456");
        CHECK(has(o, "PAIROK")); CHECK(has(o, "SERVER Rcv-\xC3\xA9"));
        o = run(J + "ping 127.0.0.1 " + p + " 999999");
        CHECK(has(o, "AUTHFAIL PIN salah"));
        fs::remove_all(recv);
        o = run(J + "send 127.0.0.1 " + p + " 123456 " + q(U(src / "dir1")) + " " + q(U(src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt"))));
        CHECK(has(o, "SENDOK"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(sameTree(src / "dir1", recv / "dir1"));
        CHECK(sameTree(src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt"), recv / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt")));
        bool peerSeen = false;
        { std::lock_guard<std::mutex> lk(sr.m); for (auto& s : sr.logs) if (has(s, "Java-\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80")) peerSeen = true; }
        CHECK(peerSeen);  // nama dengan surrogat terbaca benar lewat writeUTF Java
        o = run(J + "evil 127.0.0.1 " + p + " 123456");
        CHECK(has(o, "EVIL ok=0") && has(o, "ditolak"));
        CHECK(!fs::exists(T / "evil.txt") && !fs::exists(recv.parent_path() / "evil.txt"));
        o = run(J + "list 127.0.0.1 " + p + " 123456 /sdcard/Download");
        CHECK(has(o, "ENT dl.txt 321 0"));
        o = run(J + "pull 127.0.0.1 " + p + " 123456 " + U(T / "pulled2") + " /sdcard/Download");
        CHECK(has(o, "PULLOK")); CHECK(sameTree(share / "Download", T / "pulled2" / "Download"));
        o = run(J + "push 127.0.0.1 " + p + " 123456 /sdcard/up2 " + U(src / "dir1"));
        CHECK(has(o, "PUSHOK")); std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(sameTree(src / "dir1", share / "up2" / "dir1"));

        std::puts("[5] interop: klien C++ -> server Java asli");
        fs::path jdest = T / "jdest";
        fs::create_directories(jdest);
        FILE* f = popen(("LC_ALL=C.UTF-8 java -Dfile.encoding=UTF-8 -Dstdout.encoding=UTF-8 " + javaDir + "/RefPeer.java serve 654321 " + U(jdest) + " 3 2>&1").c_str(), "r");
        char line[512];
        int jport = 0;
        while (fgets(line, sizeof line, f)) { if (std::sscanf(line, "PORT %d", &jport) == 1) break; }
        CHECK(jport > 0);
        CHECK(has(S.checkPair("127.0.0.1", jport, "000000", "Snd"), "PIN salah"));
        CHECK(S.checkPair("127.0.0.1", jport, "654321", "Snd-\xF0\x9F\x98\x80\xE4\xB8\xAD").empty());
        ss.reset();
        S.send("127.0.0.1", jport, "654321", "Snd-\xF0\x9F\x98\x80\xE4\xB8\xAD", {src / "dir1", src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt")});
        CHECK(ss.wait()); CHECK(ss.ok);
        std::string rest;
        while (fgets(line, sizeof line, f)) rest += line;
        pclose(f);
        CHECK(has(rest, "PEER Snd-\xF0\x9F\x98\x80\xE4\xB8\xAD mode=2"));
        CHECK(has(rest, "DONE"));
        CHECK(sameTree(src / "dir1", jdest / "dir1"));
        CHECK(sameTree(src / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt"), jdest / P("\xC3\xBCn\xC3\xAF \xE4\xB8\xAD.txt")));
    }

    std::puts("[6] 5x PIN salah menghentikan server");
    for (int i = 0; i < 5; i++) S.checkPair("127.0.0.1", port, "bad", "x");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(!R.listening());

    R.stopListen();
    std::error_code ec;
    fs::remove_all(T, ec);
    std::printf(g_fail ? "\nSELESAI: %d GAGAL\n" : "\nSELESAI: semua lulus\n", g_fail);
    return g_fail ? 1 : 0;
}
