// Uji orkestrator update dengan IO palsu (tanpa jaringan). Build: lihat CMakeLists (target ng_updater_test).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../src/core/sha256.h"
#include "../src/core/updater.h"

using namespace ng;
namespace fs = std::filesystem;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::string hashOf(const std::string& s) {
    Sha256 h; h.update(s.data(), s.size());
    uint8_t d[32]; h.final(d);
    return toHex(d, 32);
}

struct Fake {
    std::string releasesJson, exeBytes, shaText;
    bool applied = false;
    std::string appliedFile;
    UpdaterIO io(const std::string& tmp) {
        UpdaterIO o;
        o.httpGet = [this](const std::string& url, std::string& body, std::string& err) {
            if (url.find("releases") != std::string::npos) { body = releasesJson; return true; }
            if (url.find(".sha256") != std::string::npos) { body = shaText; return true; }
            err = "404"; return false;
        };
        o.httpDownload = [this](const std::string&, const std::string& dest, const std::function<void(int64_t, int64_t)>& pr,
                                const std::atomic<bool>&, std::string&) {
            std::ofstream(fs::u8path(dest), std::ios::binary) << exeBytes;
            pr((int64_t)exeBytes.size(), (int64_t)exeBytes.size());
            return true;
        };
        o.applyUpdate = [this](const std::string& f, std::string&) { applied = true; appliedFile = f; return true; };
        o.tempDir = [tmp] { return tmp; };
        return o;
    }
};

static std::string rel(const std::string& tag, bool pre, const std::string& extraAssetFields, bool withShaAsset, size_t size,
                       const std::string& exeName = "NgirimPC.exe") {
    std::ostringstream o;
    o << "[{\"tag_name\":\"" << tag << "\",\"draft\":false,\"prerelease\":" << (pre ? "true" : "false")
      << ",\"body\":\"catatan\",\"assets\":[{\"name\":\"" << exeName << "\",\"browser_download_url\":\"https://x/" << exeName << "\",\"size\":"
      << size << extraAssetFields << "}";
    if (withShaAsset) o << ",{\"name\":\"" << exeName << ".sha256\",\"browser_download_url\":\"https://x/" << exeName << ".sha256\"}";
    o << "]}]";
    return o.str();
}

static UpdSnapshot run(Fake& f, const std::string& tmp, const char* cur, bool pre, bool download) {
    Updater u(cur, f.io(tmp), "https://api.example/releases");
    u.check(pre); u.wait();
    if (download && u.snapshot().state == UpdState::Available) { u.download(); u.wait(); }
    UpdSnapshot s = u.snapshot();
    if (s.state == UpdState::Ready) { std::string e; CHECK(u.apply(e)); }
    return s;
}

int main() {
    const std::string tmp = (fs::temp_directory_path() / "ngupd_test").string();
    fs::create_directories(tmp);
    const std::string exe = "MZ" + std::string(5000, 'x');
    const std::string good = hashOf(exe);

    { // sudah terbaru
        Fake f; f.exeBytes = exe;
        f.releasesJson = rel("v2.0.0", false, ",\"digest\":\"sha256:" + good + "\"", false, exe.size());
        CHECK(run(f, tmp, "2.0.0", false, true).state == UpdState::UpToDate);
    }
    { // jalur sukses lewat field digest
        Fake f; f.exeBytes = exe;
        f.releasesJson = rel("v2.1.0", false, ",\"digest\":\"sha256:" + good + "\"", false, exe.size());
        UpdSnapshot s = run(f, tmp, "2.0.0", false, true);
        CHECK(s.state == UpdState::Ready && f.applied && s.frac == 1.0);
        CHECK(f.appliedFile.find("NgirimPC-v2.1.0.exe") != std::string::npos);
    }
    { // jalur sukses lewat aset .sha256 ("hex  nama")
        Fake f; f.exeBytes = exe; f.shaText = good + "  NgirimPC.exe\n";
        f.releasesJson = rel("v2.1.0", false, "", true, exe.size());
        CHECK(run(f, tmp, "2.0.0", false, true).state == UpdState::Ready);
    }
    { // nama exe memuat versi (NgirimPC-v2.1.0.exe) + sha256 terpisah
        Fake f; f.exeBytes = exe; f.shaText = good + "  NgirimPC-v2.1.0.exe\n";
        f.releasesJson = rel("v2.1.0", false, "", true, exe.size(), "NgirimPC-v2.1.0.exe");
        CHECK(run(f, tmp, "2.0.0", false, true).state == UpdState::Ready);
    }
    { // aset installer (NgirimPC-Setup-*.exe) tidak boleh dipilih sebagai update
        Fake f; f.exeBytes = exe;
        std::string j = rel("v2.1.0", false, ",\"digest\":\"sha256:" + good + "\"", false, exe.size(), "NgirimPC-v2.1.0.exe");
        std::string setup = "{\"name\":\"NgirimPC-Setup-v2.1.0.exe\",\"browser_download_url\":\"https://x/setup.exe\",\"size\":9}";
        j.insert(j.find("\"assets\":[") + 10, setup + ",");   // installer di urutan pertama
        f.releasesJson = j;
        UpdSnapshot s = run(f, tmp, "2.0.0", false, true);
        CHECK(s.state == UpdState::Ready && s.info.exeUrl.find("setup") == std::string::npos);
    }
    { // checksum salah -> gagal, file dihapus, tidak dipasang
        Fake f; f.exeBytes = exe;
        f.releasesJson = rel("v2.1.0", false, ",\"digest\":\"sha256:" + std::string(64, 'a') + "\"", false, exe.size());
        UpdSnapshot s = run(f, tmp, "2.0.0", false, true);
        CHECK(s.state == UpdState::Failed && !f.applied && s.error.find("Checksum") != std::string::npos);
        CHECK(!fs::exists(fs::u8path(tmp + "/NgirimPC-v2.1.0.exe")));
    }
    { // tanpa checksum sama sekali -> ditolak
        Fake f; f.exeBytes = exe;
        f.releasesJson = rel("v2.1.0", false, "", false, exe.size());
        UpdSnapshot s = run(f, tmp, "2.0.0", false, true);
        CHECK(s.state == UpdState::Failed && !f.applied);
    }
    { // ukuran tidak cocok
        Fake f; f.exeBytes = exe;
        f.releasesJson = rel("v2.1.0", false, ",\"digest\":\"sha256:" + good + "\"", false, exe.size() + 1);
        CHECK(run(f, tmp, "2.0.0", false, true).state == UpdState::Failed);
    }
    { // bukan PE walau hash cocok
        Fake f; f.exeBytes = "ZZ" + std::string(100, 'x');
        f.releasesJson = rel("v2.1.0", false, ",\"digest\":\"sha256:" + hashOf(f.exeBytes) + "\"", false, f.exeBytes.size());
        UpdSnapshot s = run(f, tmp, "2.0.0", false, true);
        CHECK(s.state == UpdState::Failed && s.error.find("bukan program") != std::string::npos);
    }
    { // pra-rilis: disembunyikan kecuali diizinkan; beta.10 > beta.2
        Fake f; f.exeBytes = exe;
        f.releasesJson = rel("v2.1.0-beta.10", true, ",\"digest\":\"sha256:" + good + "\"", false, exe.size());
        CHECK(run(f, tmp, "2.0.0", false, false).state == UpdState::UpToDate);
        CHECK(run(f, tmp, "2.0.0", true, false).state == UpdState::Available);
        CHECK(run(f, tmp, "2.1.0-beta.2", true, false).state == UpdState::Available);
        CHECK(run(f, tmp, "2.1.0", true, false).state == UpdState::UpToDate);
    }
    { // respons rusak / kena rate limit
        Fake f; f.releasesJson = "{\"message\":\"API rate limit exceeded\"}";
        UpdSnapshot s = run(f, tmp, "2.0.0", false, false);
        CHECK(s.state == UpdState::Failed && s.error.find("rate limit") != std::string::npos);
    }
    { // apply sebelum Ready ditolak
        Fake f; Updater u("2.0.0", f.io(tmp), "x");
        std::string e; CHECK(!u.apply(e));
    }

    fs::remove_all(tmp);
    std::puts(fails ? "ADA TES GAGAL" : "SELESAI: updater lulus");
    return fails ? 1 : 0;
}
