#include "updater_core.h"
#include <cstring>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ng {

// ------------------------------------------------------------------ JSON mini
namespace {
struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    explicit Parser(const std::string& t) : s(t) {}

    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    static void putUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("string diharapkan");
        i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t v;
                if (!hex4(v)) return fail("escape \\u salah");
                if (v >= 0xD800 && v < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    size_t save = i;
                    i += 2;
                    uint32_t lo;
                    if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                    else i = save;
                }
                putUtf8(out, v);
                break;
            }
            default: return fail("escape tidak dikenal");
            }
        }
        return fail("string tidak ditutup");
    }
    bool value(Json& j, int depth) {
        if (depth > 64) return fail("terlalu dalam");
        ws();
        if (i >= s.size()) return fail("data habis");
        char c = s[i];
        if (c == '{') {
            j.t = Json::Obj; i++; ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            while (true) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i++] != ':') return fail("':' diharapkan");
                Json v;
                if (!value(v, depth + 1)) return false;
                j.o.emplace_back(std::move(k), std::move(v));
                ws();
                if (i >= s.size()) return fail("data habis");
                if (s[i] == ',') { i++; continue; }
                if (s[i] == '}') { i++; return true; }
                return fail("',' atau '}' diharapkan");
            }
        }
        if (c == '[') {
            j.t = Json::Arr; i++; ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                j.a.push_back(std::move(v));
                ws();
                if (i >= s.size()) return fail("data habis");
                if (s[i] == ',') { i++; continue; }
                if (s[i] == ']') { i++; return true; }
                return fail("',' atau ']' diharapkan");
            }
        }
        if (c == '"') { j.t = Json::Str; return str(j.s); }
        if (s.compare(i, 4, "true") == 0) { j.t = Json::Bool; j.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { j.t = Json::Bool; j.b = false; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { j.t = Json::Null; i += 4; return true; }
        size_t st = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || std::strchr("+-.eE", s[i]))) i++;
        if (st == i) return fail("token tidak dikenal");
        j.t = Json::Num;
        j.n = std::strtod(s.substr(st, i - st).c_str(), nullptr);
        return true;
    }
};
}  // namespace

const Json* Json::get(const std::string& key) const {
    for (auto& kv : o) if (kv.first == key) return &kv.second;
    return nullptr;
}
std::string Json::str(const std::string& key) const {
    const Json* v = get(key);
    return v && v->t == Str ? v->s : std::string();
}
bool Json::flag(const std::string& key) const {
    const Json* v = get(key);
    return v && v->t == Bool && v->b;
}

bool parseJson(const std::string& text, Json& out, std::string& err) {
    Parser p(text);
    out = Json();
    if (!p.value(out, 0)) { err = p.err; return false; }
    return true;
}

// ------------------------------------------------------------------ versi
namespace {
struct Ver { std::vector<long> nums; std::string pre; };
Ver parseVer(std::string v) {
    Ver r;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    size_t cut = v.find_first_of("-+");
    std::string core = cut == std::string::npos ? v : v.substr(0, cut);
    if (cut != std::string::npos && v[cut] == '-') r.pre = v.substr(cut + 1);
    size_t i = 0;
    while (i <= core.size()) {
        size_t j = core.find('.', i);
        if (j == std::string::npos) j = core.size();
        r.nums.push_back(std::atol(core.substr(i, j - i).c_str()));
        i = j + 1;
    }
    return r;
}
}  // namespace

int compareVersions(const std::string& a, const std::string& b) {
    Ver x = parseVer(a), y = parseVer(b);
    size_t n = std::max(x.nums.size(), y.nums.size());
    for (size_t i = 0; i < n; i++) {
        long p = i < x.nums.size() ? x.nums[i] : 0, q = i < y.nums.size() ? y.nums[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    if (x.pre.empty() && y.pre.empty()) return 0;
    if (x.pre.empty()) return 1;
    if (y.pre.empty()) return -1;
    // bandingkan per identifier (dipisah '.'): angka dibandingkan numerik, angka < teks, lebih pendek < lebih panjang
    auto split = [](const std::string& t) {
        std::vector<std::string> o;
        size_t i = 0;
        while (i <= t.size()) {
            size_t j = t.find('.', i);
            if (j == std::string::npos) j = t.size();
            o.push_back(t.substr(i, j - i));
            i = j + 1;
        }
        return o;
    };
    auto isNum = [](const std::string& t) {
        return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; });
    };
    std::vector<std::string> a2 = split(x.pre), b2 = split(y.pre);
    size_t m = std::min(a2.size(), b2.size());
    for (size_t i = 0; i < m; i++) {
        bool na = isNum(a2[i]), nb = isNum(b2[i]);
        if (na && nb) {
            long long p = std::atoll(a2[i].c_str()), q = std::atoll(b2[i].c_str());
            if (p != q) return p < q ? -1 : 1;
        } else if (na != nb) {
            return na ? -1 : 1;
        } else if (a2[i] != b2[i]) {
            return a2[i] < b2[i] ? -1 : 1;
        }
    }
    if (a2.size() != b2.size()) return a2.size() < b2.size() ? -1 : 1;
    return 0;
}

// ------------------------------------------------------------------ pemilihan rilis
static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

UpdateInfo pickRelease(const std::string& json, const std::string& current, bool includePre) {
    UpdateInfo info;
    Json root;
    std::string err;
    if (!parseJson(json, root, err)) { info.error = "Respons GitHub tidak valid: " + err; return info; }
    std::vector<const Json*> rels;
    if (root.t == Json::Arr) for (auto& r : root.a) rels.push_back(&r);
    else if (root.t == Json::Obj) {
        if (root.get("tag_name")) rels.push_back(&root);
        else { info.error = root.str("message").empty() ? "Respons GitHub tidak dikenal" : root.str("message"); return info; }
    }
    const Json* best = nullptr;
    std::string bestTag, bestExe, bestSha, bestDigest;
    int64_t bestSize = 0;
    for (const Json* r : rels) {
        if (r->t != Json::Obj || r->flag("draft")) continue;
        if (r->flag("prerelease") && !includePre) continue;
        std::string tag = r->str("tag_name");
        if (tag.empty()) continue;
        std::string exe, sha, digest;
        int64_t size = 0;
        const Json* assets = r->get("assets");
        if (assets && assets->t == Json::Arr) {
            // Nama exe boleh membawa versi (mis. NgirimPC-v2.0.0.exe): cocokkan "ngirimpc*.exe", sha256 = nama exe + ".sha256"
            std::string exeName;
            for (auto& as : assets->a) {
                std::string nm = lower(as.str("name"));
                if (nm.rfind("ngirimpc", 0) != 0 || nm.size() < 12 || nm.compare(nm.size() - 4, 4, ".exe") != 0) continue;
                if (!exe.empty()) break;  // ambil exe pertama saja
                exeName = nm;
                exe = as.str("browser_download_url");
                digest = lower(as.str("digest"));
                const Json* sz = as.get("size");
                size = sz && sz->t == Json::Num ? (int64_t)sz->n : 0;
            }
            if (!exe.empty())
                for (auto& as : assets->a)
                    if (lower(as.str("name")) == exeName + ".sha256") sha = as.str("browser_download_url");
        }
        if (exe.empty()) continue;  // rilis belum lengkap: lewati
        if (!best || compareVersions(tag, bestTag) > 0) { best = r; bestTag = tag; bestExe = exe; bestSha = sha; bestDigest = digest; bestSize = size; }
    }
    if (!best) return info;
    info.tag = bestTag;
    info.notes = best->str("body");
    info.exeUrl = bestExe;
    info.shaUrl = bestSha;
    if (bestDigest.rfind("sha256:", 0) == 0) info.sha256 = bestDigest.substr(7);
    info.size = bestSize;
    info.available = compareVersions(bestTag, current) > 0;
    return info;
}

}  // namespace ng
