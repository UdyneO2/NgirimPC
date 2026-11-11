// Logika pembaruan yang portabel: parser JSON mini, perbandingan versi, pemilihan rilis GitHub.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ng {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* get(const std::string& key) const;
    std::string str(const std::string& key) const;
    bool flag(const std::string& key) const;
};

bool parseJson(const std::string& text, Json& out, std::string& err);

// "v2.1.0-beta" -> bandingkan numerik; rilis tanpa akhiran lebih baru dari yang berakhiran (-beta, -rc).
int compareVersions(const std::string& a, const std::string& b);

struct UpdateInfo {
    bool available = false;
    std::string tag, notes, exeUrl, shaUrl, sha256, error;  // sha256: dari field `digest` aset GitHub bila ada (hex kecil)
    int64_t size = 0;
};

// json: hasil /releases (larik) atau /releases/latest (objek). Memilih versi tertinggi yang punya NgirimPC.exe.
UpdateInfo pickRelease(const std::string& json, const std::string& currentVersion, bool includePrerelease);

}  // namespace ng
