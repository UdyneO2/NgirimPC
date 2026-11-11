// UI Ngirim PC (Dear ImGui). Portabel: semua akses Windows lewat plat.h; transfer lewat inti C++ (src/core).
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"

#include "../core/localops.h"
#include "../core/mdns.h"
#include "../core/proto.h"
#include "../core/sha256.h"
#include "../core/updater.h"
#include "../core/updater_core.h"
#include "../win/plat.h"
#include "../win/update_io.h"

#ifndef NGIRIMPC_VERSION
#define NGIRIMPC_VERSION "0.0"
#endif

namespace app {
namespace {

constexpr const char* kVersion = NGIRIMPC_VERSION;
constexpr const char* kReleasesApi = "https://api.github.com/repos/UdyneO2/NgirimPC/releases?per_page=20";
constexpr const char* kReleasesPage = "https://github.com/UdyneO2/NgirimPC/releases";
constexpr int kTabLocal = 0, kTabWifi = 1;

namespace fs = ng::fs;

// ------------------------------------------------------------------ tipe data
struct Entry {
    std::string name;
    long long size = 0;
    long long mtime = 0;  // detik epoch
    bool dir = false;
    bool sel = false;
};

struct Pane {
    std::string path;  // lokal: path Windows ("" = Komputer ini); jauh: "/sdcard/..."
    bool remote = false;
    std::string err, filter;
    std::vector<Entry> e;
    bool dirty = true, loading = false;
    int sort = 0;  // 0 nama, 1 ukuran, 2 tanggal
    bool asc = true;
    int anchor = -1;
    char pathBuf[1024] = {0};
    bool pathSync = true;
    char filterBuf[128] = {0};
};

struct SendItem {
    std::string path;
    bool dir = false;
    bool sel = false;
};

struct Pal {
    ImVec4 ok, warn, err, accent, dim, onAccent, panel, hoverRow;
    ImU32 text = 0, border = 0;
} g_pal;

struct Shared {
    std::mutex mu;
    std::vector<std::string> logs;
    bool logDirty = false;
    bool progShow = false;
    long long done = 0, total = 0;
    double speed = 0;
    std::string cur;
    bool hasResult = false, resultOk = true;
    std::string result;
    long long lastProgMs = 0;
    bool refreshAll = false;
    struct Remote {
        bool ready = false;
        std::string path, err;
        std::vector<Entry> e;
    } rem[2];
    bool pairBusy = false, pairReady = false;
    std::string pairErr;
    bool updChecking = false, updAvail = false, updDownloading = false, updWaiting = false;
    double updFrac = 0;
    std::string updStatus;
    ng::UpdateInfo upd;
} Sh;

struct Ui {
    // pengaturan
    int theme = 0;  // 0 ikuti sistem, 1 terang, 2 gelap
    bool dark = false, showHidden = false, useTrash = true, autoUpdate = true, preRelease = false, allowBrowse = false;
    std::string devName, recvDir;
    std::vector<std::string> bookmarks;
    // file manager + FTP
    Pane fm, ftp[2];
    std::vector<std::string> clip;
    bool clipCut = false;
    bool ftpMove = false;
    // jaringan
    bool listening = false;
    int port = 0;
    std::string recvPin;
    std::vector<ng::MdnsPeer> peers;
    double peersAt = -10;
    std::string selPeer;  // nama perangkat terpilih
    char manualBuf[96] = {0};
    char pinBuf[16] = {0};
    bool paired = false;
    std::string pairKey, pairMsg;
    std::vector<SendItem> sendList;
    bool sendToDir = false;
    char remoteDirBuf[512] = "/sdcard/Download";
    // modal
    int modal = 0;  // 1 hapus, 2 ganti nama, 3 folder baru
    bool modalOpen = false;
    char nameBuf[512] = {0};
    // umum
    int tabReq = -1;
    double nextCheck = 5;
    std::vector<std::pair<std::string, std::string>> quick;  // label, path
} U;

std::unique_ptr<ng::Engine> g_eng;
std::unique_ptr<ng::Mdns> g_mdns;
ng::Callbacks g_cb;
std::atomic<bool> g_cancel{false};
std::atomic<bool> g_localBusy{false};
float g_scale = 1.f;
ImFont* g_font = nullptr;
ImFont* g_mono = nullptr;
ImFont* g_big = nullptr;

const char* kChangelog[] = {
    "v2.0.0 (dalam pengembangan)",
    "- Rilis pertama Ngirim PC untuk Windows 10/11, kompatibel dengan Ngirim Android v2.0",
    "- File manager: salin, potong, tempel, hapus (Tempat Sampah), ganti nama, folder baru, akses cepat",
    "- Wi-Fi: terima dengan PIN, perangkat aktif otomatis (mDNS), kirim file dan folder, tarik-lepas",
    "- FTP dua panel: jelajah folder perangkat Android, unduh dan unggah",
    "- Pembaruan otomatis dari rilis GitHub (cek SHA-256), tema Terang/Gelap/Sistem",
    "",
    "Catatan",
    "- Data Wi-Fi tidak dienkripsi (otentikasi PIN saja); pakai hanya di jaringan tepercaya",
    "- Saat pertama kali menerima, izinkan Windows Firewall untuk jaringan pribadi",
    "- Router dengan client isolation memblokir koneksi antar-perangkat",
};

// ------------------------------------------------------------------ helper umum
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

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

std::string fmtDate(long long sec) {
    if (sec <= 0) return "";
    time_t t = (time_t)sec;
    struct tm tmv;
#ifdef _WIN32
    if (localtime_s(&tmv, &t) != 0) return "";
#else
    localtime_r(&t, &tmv);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tmv);
    return b;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string lowerExt(const std::string& n) {
    size_t i = n.find_last_of('.');
    if (i == std::string::npos || i + 1 >= n.size()) return "";
    return lower(n.substr(i + 1));
}

std::string tailEllipsis(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t k = s.size() - (maxBytes - 3);
    while (k < s.size() && (s[k] & 0xC0) == 0x80) k++;
    return "..." + s.substr(k);
}

void setBuf(char* dst, size_t cap, const std::string& s) {
    std::snprintf(dst, cap, "%s", s.c_str());
}

void addLog(const std::string& m) {
    time_t t = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char ts[16];
    std::strftime(ts, sizeof ts, "[%H:%M:%S] ", &tmv);  // 11 karakter
    std::lock_guard<std::mutex> l(Sh.mu);
    Sh.logs.push_back(std::string(ts) + m);
    if (Sh.logs.size() > 3000) Sh.logs.erase(Sh.logs.begin(), Sh.logs.begin() + 500);
    Sh.logDirty = true;
}

std::string joinLocal(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    char c = base.back();
    return (c == '\\' || c == '/') ? base + name : base + "\\" + name;
}

std::string parentLocal(const std::string& p) {
    std::string s = p;
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    if (s.size() <= 3) return "";  // akar drive -> Komputer ini
    size_t i = s.find_last_of("\\/");
    if (i == std::string::npos) return "";
    std::string r = s.substr(0, i);
    if (r.size() == 2 && r[1] == ':') r += "\\";
    return r;
}

std::string joinRemote(const std::string& base, const std::string& name) {
    return (base.empty() || base.back() != '/') ? base + "/" + name : base + name;
}

std::string parentRemote(const std::string& p) {
    std::string s = p;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    size_t i = s.find_last_of('/');
    if (i == std::string::npos || i == 0) return "/";
    return s.substr(0, i);
}

int icmp(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        int x = std::tolower((unsigned char)a[i]), y = std::tolower((unsigned char)b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool busy() { return (g_eng && g_eng->busy()) || g_localBusy.load(); }

// ------------------------------------------------------------------ tema (gaya Windows, sama dengan versi Android)
ImVec4 rgb(int r, int g, int b, float a = 1.f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }

void applyTheme(bool dark) {
    ImVec4* c = ImGui::GetStyle().Colors;
    const ImVec4 accent = rgb(0, 120, 215);
    ImVec4 bg, panel, text, dis, border, btn, btnH, btnA, sel, selH, frame, frameH;
    if (!dark) {
        bg = rgb(255, 255, 255); panel = rgb(243, 243, 243); text = rgb(20, 20, 20); dis = rgb(112, 112, 112);
        border = rgb(173, 173, 173); btn = rgb(225, 225, 225); btnH = rgb(229, 241, 251); btnA = rgb(204, 228, 247);
        sel = rgb(204, 232, 255); selH = rgb(229, 243, 255); frame = rgb(255, 255, 255); frameH = rgb(229, 241, 251);
        g_pal.ok = rgb(16, 124, 16); g_pal.warn = rgb(196, 89, 17); g_pal.err = rgb(196, 43, 28);
    } else {
        bg = rgb(31, 31, 31); panel = rgb(43, 43, 43); text = rgb(240, 240, 240); dis = rgb(160, 160, 160);
        border = rgb(95, 95, 95); btn = rgb(51, 51, 51); btnH = rgb(65, 65, 65); btnA = rgb(80, 80, 80);
        sel = rgb(0, 74, 143); selH = rgb(52, 62, 72); frame = rgb(43, 43, 43); frameH = rgb(65, 65, 65);
        g_pal.ok = rgb(108, 203, 95); g_pal.warn = rgb(252, 185, 90); g_pal.err = rgb(255, 120, 105);
    }
    g_pal.accent = accent; g_pal.dim = dis; g_pal.onAccent = rgb(255, 255, 255); g_pal.panel = panel;
    g_pal.hoverRow = selH;
    g_pal.text = ImGui::ColorConvertFloat4ToU32(text);
    g_pal.border = ImGui::ColorConvertFloat4ToU32(border);

    c[ImGuiCol_Text] = text; c[ImGuiCol_TextDisabled] = dis;
    c[ImGuiCol_WindowBg] = bg; c[ImGuiCol_ChildBg] = bg; c[ImGuiCol_PopupBg] = bg;
    c[ImGuiCol_Border] = border; c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame; c[ImGuiCol_FrameBgHovered] = frameH; c[ImGuiCol_FrameBgActive] = btnA;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_Button] = btn; c[ImGuiCol_ButtonHovered] = btnH; c[ImGuiCol_ButtonActive] = btnA;
    c[ImGuiCol_Header] = sel; c[ImGuiCol_HeaderHovered] = selH; c[ImGuiCol_HeaderActive] = sel;
    c[ImGuiCol_Separator] = border; c[ImGuiCol_SeparatorHovered] = accent; c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_Tab] = panel; c[ImGuiCol_TabHovered] = selH; c[ImGuiCol_TabSelected] = bg;
    c[ImGuiCol_TabSelectedOverline] = accent; c[ImGuiCol_TabDimmed] = panel; c[ImGuiCol_TabDimmedSelected] = bg;
    c[ImGuiCol_TabDimmedSelectedOverline] = accent;
    c[ImGuiCol_PlotHistogram] = rgb(6, 176, 37);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = border;
    c[ImGuiCol_TextSelectedBg] = sel;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
}

void refreshTheme() {
    U.dark = U.theme == 2 || (U.theme == 0 && plat::systemPrefersDark());
    applyTheme(U.dark);
}

void setupStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = 0; st.ChildRounding = 0; st.FrameRounding = 0; st.PopupRounding = 0;
    st.ScrollbarRounding = 0; st.GrabRounding = 0; st.TabRounding = 0;
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.FrameBorderSize = 1; st.PopupBorderSize = 1;
    st.WindowPadding = ImVec2(12, 10);
    st.FramePadding = ImVec2(10, 5);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.ScrollbarSize = 14;
    st.GrabMinSize = 10;
    st.TabBarBorderSize = 1;
    st.ScaleAllSizes(scale);
}

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar latin[] = {0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x052F, 0x2000, 0x206F,
                                    0x20A0, 0x20CF, 0x2190, 0x21FF, 0};
    static const ImWchar ascii[] = {0x0020, 0x007E, 0};
    const std::string dir = "C:\\Windows\\Fonts\\";
    const float px = 17.f * scale;
    const std::string seg = dir + "segoeui.ttf";
    if (plat::fileExists(seg)) g_font = io.Fonts->AddFontFromFileTTF(seg.c_str(), px, nullptr, latin);
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    const std::string cjk = dir + "msyh.ttc";  // nama file CJK (Mandarin/Jepang) bila ada
    if (plat::fileExists(cjk)) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(cjk.c_str(), px, &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    const std::string mono = dir + "consola.ttf";
    if (plat::fileExists(mono)) g_mono = io.Fonts->AddFontFromFileTTF(mono.c_str(), px * 0.92f, nullptr, latin);
    if (plat::fileExists(seg)) g_big = io.Fonts->AddFontFromFileTTF(seg.c_str(), px * 2.1f, nullptr, ascii);
}

// ------------------------------------------------------------------ pengaturan (settings.ini)
std::string settingsPath() { return plat::dataDir() + "\\settings.ini"; }

void loadSettings() {
    std::ifstream f(fs::u8path(settingsPath()));
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = trim(line.substr(eq + 1));
        if (k == "theme") U.theme = std::max(0, std::min(2, std::atoi(v.c_str())));
        else if (k == "hidden") U.showHidden = v == "1";
        else if (k == "trash") U.useTrash = v == "1";
        else if (k == "auto") U.autoUpdate = v == "1";
        else if (k == "pre") U.preRelease = v == "1";
        else if (k == "browse") U.allowBrowse = v == "1";
        else if (k == "name" && !v.empty()) U.devName = v;
        else if (k == "recv" && !v.empty()) U.recvDir = v;
        else if (k == "bm" && !v.empty()) U.bookmarks.push_back(v);
    }
}

void saveSettings() {
    std::ofstream f(fs::u8path(settingsPath()), std::ios::trunc);
    if (!f) return;
    f << "theme=" << U.theme << "\nhidden=" << (U.showHidden ? 1 : 0) << "\ntrash=" << (U.useTrash ? 1 : 0)
      << "\nauto=" << (U.autoUpdate ? 1 : 0) << "\npre=" << (U.preRelease ? 1 : 0)
      << "\nbrowse=" << (U.allowBrowse ? 1 : 0) << "\nname=" << U.devName << "\nrecv=" << U.recvDir << "\n";
    for (auto& b : U.bookmarks) f << "bm=" << b << "\n";
}

// ------------------------------------------------------------------ pembaruan (UI)
std::unique_ptr<ng::Updater> g_upd;
bool g_updManual = false, g_updDismissed = false, g_autoChecked = false;
std::string g_applyErr;

void startCheck(bool manual) {
    if (!g_upd) return;
    g_updManual = manual;
    if (manual) { g_updDismissed = false; g_applyErr.clear(); }
    g_upd->check(U.preRelease);
}

void drawUpdateBanner() {
    if (!g_upd) return;
    using St = ng::UpdState;
    const ng::UpdSnapshot s = g_upd->snapshot();
    bool show = false;
    switch (s.state) {
        case St::Available: show = !g_updDismissed; break;
        case St::Downloading: case St::Verifying: case St::Ready: show = true; break;
        case St::Checking: case St::UpToDate: case St::Failed: show = g_updManual; break;
        default: break;
    }
    if (!show) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_pal.accent);
    ImGui::PushStyleColor(ImGuiCol_Text, g_pal.onAccent);
    ImGui::BeginChild("##upd", ImVec2(0, ImGui::GetFrameHeight() * 1.7f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    const float right = ImGui::GetContentRegionAvail().x;
    auto btnAt = [&](const char* label, float fromRight) {
        ImGui::SameLine(right - fromRight);
        return ImGui::Button(label);
    };
    const float bw = 150.f * g_scale;
    switch (s.state) {
        case St::Available: {
            std::string t = "Versi baru " + s.info.tag + " tersedia";
            if (s.info.size > 0) t += " (" + fmtSize((double)s.info.size) + ")";
            ImGui::TextUnformatted(t.c_str());
            if (btnAt("Perbarui", bw)) g_upd->download();
            if (btnAt("Nanti", bw * 0.5f)) g_updDismissed = true;
            break;
        }
        case St::Downloading: {
            char ov[96];
            std::snprintf(ov, sizeof ov, "Mengunduh %d%% (%s / %s)", (int)(s.frac * 100), fmtSize((double)s.done).c_str(),
                          fmtSize((double)s.total).c_str());
            ImGui::ProgressBar((float)s.frac, ImVec2(right - bw, 0), ov);
            if (btnAt("Batal", bw * 0.6f)) g_upd->cancel();
            break;
        }
        case St::Verifying:
            ImGui::TextUnformatted("Memverifikasi SHA-256...");
            break;
        case St::Ready:
            ImGui::TextUnformatted(g_applyErr.empty() ? "Pembaruan siap dipasang" : g_applyErr.c_str());
            if (btnAt("Pasang dan mulai ulang", bw * 1.9f)) {
                std::string err;
                if (g_upd->apply(err)) plat::requestQuit();
                else { g_applyErr = err; addLog("ERR " + err); }
            }
            break;
        case St::Checking:
            ImGui::TextUnformatted("Memeriksa pembaruan...");
            break;
        case St::UpToDate:
            ImGui::Text("Sudah versi terbaru (%s)", kVersion);
            if (btnAt("OK", bw * 0.4f)) g_updManual = false;
            break;
        case St::Failed:
            ImGui::TextUnformatted(("Pembaruan gagal: " + s.error).c_str());
            if (btnAt("Tutup", bw * 0.6f)) g_updManual = false;
            break;
        default: break;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// ------------------------------------------------------------------ daftar folder lokal
void refreshPane(Pane& p) {
    p.e.clear();
    p.err.clear();
    if (p.path.empty()) {
        for (auto& d : plat::drives()) { Entry e; e.name = d; e.dir = true; p.e.push_back(e); }
    } else {
        std::string err;
        for (auto& le : ng::listDir(ng::P(p.path), err)) {
            if (le.hidden && !U.showHidden) continue;
            Entry e; e.name = le.name; e.size = le.size; e.mtime = le.mtime; e.dir = le.dir;
            p.e.push_back(e);
        }
        p.err = err;
    }
    std::sort(p.e.begin(), p.e.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return icmp(a.name, b.name) < 0;
    });
    p.dirty = false;
}

void goTo(Pane& p, const std::string& path) { p.path = path; p.dirty = true; }

// ------------------------------------------------------------------ transfer: callback bersama
std::string g_host;
int g_port = 0;
std::vector<std::string> g_modalPaths;

void initCallbacks() {
    g_cb.log = [](const std::string& m) { addLog(m); };
    g_cb.progress = [](const std::string& cur, int64_t d, int64_t t, double sp) {
        std::lock_guard<std::mutex> l(Sh.mu);
        Sh.progShow = true; Sh.cur = cur; Sh.done = d; Sh.total = t; Sh.speed = sp;
    };
    g_cb.finish = [](bool ok, const std::string& msg, int64_t, int64_t) {
        {
            std::lock_guard<std::mutex> l(Sh.mu);
            Sh.progShow = false; Sh.hasResult = true; Sh.resultOk = ok; Sh.result = msg; Sh.refreshAll = true;
        }
        addLog((ok ? "OK     " : "ERR    ") + msg);
    };
}

void runLocal(std::function<void()> job) {
    if (busy()) { addLog("ERR operasi lain masih berjalan"); return; }
    g_localBusy = true;
    g_cancel = false;
    {
        std::lock_guard<std::mutex> l(Sh.mu);
        Sh.progShow = true; Sh.hasResult = false; Sh.done = Sh.total = 0; Sh.cur.clear(); Sh.speed = 0;
    }
    std::thread([job] { job(); g_localBusy = false; }).detach();
}

void sortEntries(std::vector<Entry>& v) {
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return icmp(a.name, b.name) < 0;
    });
}

std::vector<std::string> selectedPaths(const Pane& p) {
    std::vector<std::string> v;
    for (auto& e : p.e)
        if (e.sel) v.push_back(p.remote ? joinRemote(p.path, e.name) : joinLocal(p.path, e.name));
    return v;
}

void drawProgress() {
    bool show, hasRes, ok;
    std::string cur, res;
    long long d, t;
    double sp;
    {
        std::lock_guard<std::mutex> l(Sh.mu);
        show = Sh.progShow; hasRes = Sh.hasResult; ok = Sh.resultOk; cur = Sh.cur; res = Sh.result;
        d = Sh.done; t = Sh.total; sp = Sh.speed;
    }
    if (show) {
        const float frac = t > 0 ? (float)((double)d / (double)t) : 0.f;
        char ov[160];
        std::snprintf(ov, sizeof ov, "%d%%  %s / %s  %s/s  sisa %s", (int)(frac * 100), fmtSize((double)d).c_str(),
                      fmtSize((double)t).c_str(), fmtSize(sp).c_str(), fmtEta(sp > 0 ? (double)(t - d) / sp : -1).c_str());
        ImGui::ProgressBar(frac, ImVec2(-100.f * g_scale, 0), ov);
        ImGui::SameLine();
        if (ImGui::Button("Batal")) { g_cancel = true; if (g_eng) g_eng->cancel(); }
        if (!cur.empty()) ImGui::TextColored(g_pal.dim, "%s", tailEllipsis(cur, 110).c_str());
    } else if (hasRes) {
        ImGui::TextColored(ok ? g_pal.ok : g_pal.err, "%s", res.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Tutup")) { std::lock_guard<std::mutex> l(Sh.mu); Sh.hasResult = false; }
    }
}

// ------------------------------------------------------------------ tabel file (dipakai Lokal dan FTP)
bool drawPane(Pane& p, const char* id, const ImVec2& size, std::string& navTo) {
    bool nav = false;
    if (!p.err.empty()) ImGui::TextColored(g_pal.err, "%s", p.err.c_str());
    if (ImGui::BeginTable(id, 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable, size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nama", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ukuran", ImGuiTableColumnFlags_WidthFixed, 90.f * g_scale);
        ImGui::TableSetupColumn("Diubah", ImGuiTableColumnFlags_WidthFixed, 140.f * g_scale);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)p.e.size(); i++) {
            Entry& e = p.e[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (e.dir) ImGui::PushStyleColor(ImGuiCol_Text, g_pal.accent);
            if (ImGui::Selectable(e.name.c_str(), e.sel,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyShift && p.anchor >= 0 && p.anchor < (int)p.e.size()) {
                    int a = std::min(p.anchor, i), b = std::max(p.anchor, i);
                    for (int k = 0; k < (int)p.e.size(); k++) p.e[k].sel = k >= a && k <= b;
                } else if (io.KeyCtrl) {
                    e.sel = !e.sel;
                    p.anchor = i;
                } else {
                    for (auto& o : p.e) o.sel = false;
                    e.sel = true;
                    p.anchor = i;
                }
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (e.dir) { navTo = p.remote ? joinRemote(p.path, e.name) : joinLocal(p.path, e.name); nav = true; }
                    else if (!p.remote) plat::openPath(joinLocal(p.path, e.name));
                }
            }
            if (e.dir) ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            if (!e.dir) ImGui::TextUnformatted(fmtSize((double)e.size).c_str());
            ImGui::TableSetColumnIndex(2);
            if (e.mtime > 0) ImGui::TextUnformatted(fmtDate(e.mtime).c_str());
        }
        ImGui::EndTable();
    }
    return nav;
}

void openModal(int kind, const std::vector<std::string>& paths) {
    U.modal = kind;
    U.modalOpen = true;
    g_modalPaths = paths;
    if (kind == 2 && !paths.empty()) {
        std::string n = paths[0];
        size_t i = n.find_last_of("\\/");
        setBuf(U.nameBuf, sizeof U.nameBuf, i == std::string::npos ? n : n.substr(i + 1));
    } else {
        U.nameBuf[0] = 0;
    }
}

// ------------------------------------------------------------------ tab Lokal
void drawLocal() {
    Pane& p = U.fm;
    if (U.quick.empty()) {
        U.quick = {{"Desktop", plat::knownFolder("desktop")}, {"Dokumen", plat::knownFolder("documents")},
                   {"Unduhan", plat::knownFolder("downloads")}, {"Gambar", plat::knownFolder("pictures")}};
        for (auto& d : plat::drives()) U.quick.push_back({"Disk " + d, d});
        for (auto& b : U.bookmarks) U.quick.push_back({"* " + tailEllipsis(b, 24), b});
        p.path = plat::knownFolder("downloads");
        p.dirty = true;
    }
    if (p.dirty) refreshPane(p);

    std::string go;
    bool nav = false;
    ImGui::BeginChild("##quick", ImVec2(170.f * g_scale, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(g_pal.dim, "Akses cepat");
    for (auto& q : U.quick)
        if (ImGui::Selectable(q.first.c_str(), p.path == q.second)) { go = q.second; nav = true; }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (ImGui::Button("Naik")) { go = parentLocal(p.path); nav = true; }
    ImGui::SameLine();
    if (ImGui::Button("Segarkan")) p.dirty = true;
    ImGui::SameLine();
    ImGui::TextUnformatted(p.path.empty() ? "Komputer ini" : p.path.c_str());

    const std::vector<std::string> sel = selectedPaths(p);
    const bool has = !sel.empty(), inFolder = !p.path.empty(), bz = busy();
    auto tb = [&](const char* label, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        bool r = ImGui::Button(label);
        ImGui::EndDisabled();
        ImGui::SameLine();
        return r;
    };
    if (tb("Salin", has)) { U.clip = sel; U.clipCut = false; addLog("OK     " + std::to_string(sel.size()) + " item disalin ke clipboard"); }
    if (tb("Potong", has)) { U.clip = sel; U.clipCut = true; addLog("OK     " + std::to_string(sel.size()) + " item dipotong"); }
    if (tb("Tempel", !U.clip.empty() && inFolder && !bz)) {
        std::vector<fs::path> src;
        for (auto& s : U.clip) src.push_back(ng::P(s));
        const fs::path dst = ng::P(p.path);
        const bool mv = U.clipCut;
        if (mv) U.clip.clear();
        runLocal([src, dst, mv] { ng::copyPaths(src, dst, mv, g_cb, g_cancel); });
    }
    if (tb("Hapus", has && !bz)) openModal(1, sel);
    if (tb("Ganti nama", sel.size() == 1)) openModal(2, sel);
    if (tb("Folder baru", inFolder)) openModal(3, {p.path});
    if (tb("Kirim ke Wi-Fi", has)) {
        for (auto& s : sel) {
            SendItem it;
            it.path = s;
            std::error_code ec;
            it.dir = fs::is_directory(ng::P(s), ec);
            U.sendList.push_back(it);
        }
        U.tabReq = kTabWifi;
    }
    ImGui::NewLine();

    std::string to;
    if (drawPane(p, "##files", ImVec2(0, 0), to)) { go = to; nav = true; }
    ImGui::EndGroup();
    if (nav) goTo(p, go);
}

// ------------------------------------------------------------------ pairing dan penerimaan
bool resolveTarget(std::string& host, int& port) {
    const std::string m = trim(U.manualBuf);
    if (!m.empty()) {
        size_t c = m.rfind(':');
        if (c == std::string::npos) return false;
        host = m.substr(0, c);
        port = std::atoi(m.substr(c + 1).c_str());
        return !host.empty() && port > 0 && port < 65536;
    }
    for (auto& pe : U.peers)
        if (pe.name == U.selPeer) { host = pe.host; port = pe.port; return true; }
    return false;
}

void startPair(const std::string& host, int port, const std::string& pin) {
    {
        std::lock_guard<std::mutex> l(Sh.mu);
        if (Sh.pairBusy) return;
        Sh.pairBusy = true; Sh.pairReady = false;
    }
    const std::string name = U.devName;
    U.paired = false;
    std::thread([host, port, pin, name] {
        std::string e = g_eng ? g_eng->checkPair(host, port, pin, name) : "Mesin belum siap";
        std::lock_guard<std::mutex> l(Sh.mu);
        Sh.pairBusy = false; Sh.pairReady = true; Sh.pairErr = e;
    }).detach();
    g_host = host;
    g_port = port;
    U.pairKey = pin;
    U.pairMsg = "Menghubungkan...";
}

void pollPair() {
    bool ready;
    std::string err;
    {
        std::lock_guard<std::mutex> l(Sh.mu);
        ready = Sh.pairReady; err = Sh.pairErr;
        Sh.pairReady = false;
    }
    if (!ready) return;
    U.paired = err.empty();
    U.pairMsg = err.empty() ? "Terhubung ke " + g_host + ":" + std::to_string(g_port) : err;
    addLog(err.empty() ? "OK     pairing berhasil" : "ERR    pairing: " + err);
    if (U.paired) { U.ftp[1].dirty = true; U.ftp[1].path = "/sdcard"; }
}

void toggleListen(bool on) {
    if (!g_eng) return;
    if (!on) {
        g_eng->stopListen();
        if (g_mdns) g_mdns->unannounce();
        U.listening = false;
        addLog("OK     penerimaan dimatikan");
        return;
    }
    g_eng->setAllowBrowse(U.allowBrowse);
    g_eng->setShareRoot(ng::P(plat::knownFolder("home")));
    const std::string pin = ng::randomPin();
    const int port = g_eng->listen(ng::P(U.recvDir), pin, U.devName);
    if (port > 0) {
        U.listening = true; U.port = port; U.recvPin = pin;
        if (g_mdns) g_mdns->announce(U.devName, port);
        addLog("OK     menerima di port " + std::to_string(port));
    } else {
        U.listening = false;
        addLog("ERR    gagal membuka port penerimaan");
    }
}

void refreshPeers() {
    if (!g_mdns) return;
    const double now = ImGui::GetTime();
    if (now - U.peersAt < 4.0) return;
    U.peersAt = now;
    g_mdns->query();
    U.peers.clear();
    for (auto& pe : g_mdns->peers())
        if (!(U.listening && pe.port == U.port && pe.name == U.devName)) U.peers.push_back(pe);
}

// ------------------------------------------------------------------ tab Wi-Fi
void drawWifi() {
    refreshPeers();
    pollPair();

    ImGui::BeginChild("##recv", ImVec2(380.f * g_scale, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(g_pal.dim, "Terima");
    bool on = U.listening;
    if (ImGui::Checkbox("Aktifkan penerimaan", &on)) toggleListen(on);
    if (U.listening) {
        static std::vector<std::string> ips;
        static double ipsAt = -100;
        if (ImGui::GetTime() - ipsAt > 10.0) { ips = ng::localIPv4(); ipsAt = ImGui::GetTime(); }
        if (ips.empty()) ImGui::TextColored(g_pal.warn, "Tidak ada jaringan aktif (hubungkan ke Wi-Fi/LAN)");
        for (auto& ip : ips) ImGui::Text("Alamat: %s:%d", ip.c_str(), U.port);
        if (!ips.empty() && ImGui::SmallButton("Salin alamat")) ImGui::SetClipboardText((ips[0] + ":" + std::to_string(U.port)).c_str());
        ImGui::TextColored(g_pal.dim, "PIN (masukkan di perangkat pengirim)");
        if (g_big) ImGui::PushFont(g_big);
        ImGui::TextColored(g_pal.accent, "%s", U.recvPin.c_str());
        if (g_big) ImGui::PopFont();
    }
    ImGui::Separator();
    ImGui::TextColored(g_pal.dim, "Folder simpan");
    ImGui::TextWrapped("%s", U.recvDir.c_str());
    if (ImGui::Button("Ubah")) {
        std::string d = plat::pickFolder("Pilih folder penyimpanan");
        if (!d.empty()) { U.recvDir = d; saveSettings(); if (U.listening) { toggleListen(false); toggleListen(true); } }
    }
    ImGui::SameLine();
    if (ImGui::Button("Buka")) plat::openPath(U.recvDir);
    ImGui::Separator();
    ImGui::TextWrapped("Pertama kali menerima, izinkan Windows Firewall untuk jaringan pribadi. "
                       "Data Wi-Fi tidak dienkripsi (otentikasi PIN saja): pakai di jaringan tepercaya.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##send", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(g_pal.dim, "Perangkat aktif");
    if (U.peers.empty()) ImGui::TextColored(g_pal.dim, "Belum ada. Buka Ngirim di Android dan aktifkan Terima.");
    for (auto& pe : U.peers) {
        const std::string label = pe.name + "   " + pe.host + ":" + std::to_string(pe.port);
        if (ImGui::Selectable(label.c_str(), U.selPeer == pe.name)) { U.selPeer = pe.name; U.manualBuf[0] = 0; }
    }
    ImGui::SetNextItemWidth(220.f * g_scale);
    ImGui::InputText("Alamat manual (IP:port)", U.manualBuf, sizeof U.manualBuf);
    ImGui::SetNextItemWidth(120.f * g_scale);
    ImGui::InputText("PIN", U.pinBuf, sizeof U.pinBuf, ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    bool pairBusy;
    { std::lock_guard<std::mutex> l(Sh.mu); pairBusy = Sh.pairBusy; }
    ImGui::BeginDisabled(pairBusy);
    if (ImGui::Button("Pairing")) {
        std::string h;
        int pt = 0;
        if (!resolveTarget(h, pt)) U.pairMsg = "Pilih perangkat atau isi alamat dengan format IP:port";
        else if (std::strlen(U.pinBuf) < 4) U.pairMsg = "Isi PIN dari perangkat penerima";
        else startPair(h, pt, U.pinBuf);
    }
    ImGui::EndDisabled();
    if (!U.pairMsg.empty()) ImGui::TextColored(U.paired ? g_pal.ok : g_pal.dim, "%s", U.pairMsg.c_str());
    if (U.paired) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Putuskan")) { U.paired = false; U.pairMsg.clear(); U.ftp[1].e.clear(); addLog("OK     pairing diputuskan"); }
    }

    ImGui::Separator();
    ImGui::TextColored(g_pal.dim, "Daftar kirim (tarik-lepas file ke jendela ini)");
    if (ImGui::Button("Tambah file")) {
        for (auto& f : plat::pickFiles()) { SendItem it; it.path = f; U.sendList.push_back(it); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Tambah folder")) {
        std::string d = plat::pickFolder("Pilih folder yang dikirim");
        if (!d.empty()) { SendItem it; it.path = d; it.dir = true; U.sendList.push_back(it); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Kosongkan")) U.sendList.clear();
    ImGui::BeginChild("##sendlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.4f), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)U.sendList.size(); i++) {
        ImGui::PushID(i);
        const std::string t = (U.sendList[i].dir ? "[folder] " : "") + tailEllipsis(U.sendList[i].path, 90);
        if (ImGui::Selectable(t.c_str(), U.sendList[i].sel)) U.sendList[i].sel = !U.sendList[i].sel;
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button("Hapus terpilih")) {
        U.sendList.erase(std::remove_if(U.sendList.begin(), U.sendList.end(), [](const SendItem& s) { return s.sel; }),
                         U.sendList.end());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!U.paired || U.sendList.empty() || busy());
    if (ImGui::Button("Kirim")) {
        std::vector<fs::path> src;
        for (auto& s : U.sendList) src.push_back(ng::P(s.path));
        g_eng->send(g_host, g_port, U.pairKey, U.devName, src);
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

// ------------------------------------------------------------------ tab FTP (dua panel)
void remoteRefresh(Pane& p) {
    if (p.loading || !g_eng) return;
    p.loading = true;
    p.dirty = false;
    const std::string path = p.path.empty() ? "/sdcard" : p.path;
    const std::string host = g_host, pin = U.pairKey, name = U.devName;
    const int port = g_port;
    std::thread([host, port, pin, name, path] {
        std::string err;
        auto v = g_eng->remoteList(host, port, pin, name, path, err);
        std::vector<Entry> es;
        for (auto& x : v) { Entry e; e.name = x.name; e.size = x.size; e.dir = x.dir; es.push_back(e); }
        sortEntries(es);
        std::lock_guard<std::mutex> l(Sh.mu);
        Sh.rem[0].e = es; Sh.rem[0].path = path; Sh.rem[0].err = err; Sh.rem[0].ready = true;
    }).detach();
}

void drawFtp() {
    refreshPeers();
    pollPair();
    if (!U.paired) {
        ImGui::TextUnformatted("Lakukan pairing dulu di tab Wi-Fi, lalu folder perangkat Android bisa dijelajahi di sini.");
        return;
    }
    Pane& L = U.ftp[0];
    Pane& R = U.ftp[1];
    R.remote = true;
    if (L.path.empty() && L.dirty) { L.path = plat::knownFolder("downloads"); }
    if (L.dirty) refreshPane(L);
    if (R.dirty) remoteRefresh(R);
    {
        Shared::Remote r;
        bool got = false;
        {
            std::lock_guard<std::mutex> l(Sh.mu);
            if (Sh.rem[0].ready) { r = Sh.rem[0]; Sh.rem[0].ready = false; got = true; }
        }
        if (got) { R.e = r.e; R.path = r.path; R.err = r.err; R.loading = false; R.anchor = -1; }
    }

    const bool bz = busy();
    const std::vector<std::string> selL = selectedPaths(L), selR = selectedPaths(R);
    ImGui::BeginDisabled(selL.empty() || bz || R.loading);
    if (ImGui::Button("Unggah ke Android >>")) {
        std::vector<fs::path> src;
        for (auto& s : selL) src.push_back(ng::P(s));
        g_eng->push(g_host, g_port, U.pairKey, U.devName, src, R.path);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selR.empty() || bz || L.path.empty());
    if (ImGui::Button("<< Unduh ke PC")) g_eng->pull(g_host, g_port, U.pairKey, U.devName, selR, ng::P(L.path));
    ImGui::EndDisabled();

    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    std::string to;
    ImGui::BeginChild("##ftpL", ImVec2(w, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(g_pal.dim, "PC");
    if (ImGui::Button("Naik##l")) goTo(L, parentLocal(L.path));
    ImGui::SameLine();
    ImGui::TextUnformatted(L.path.empty() ? "Komputer ini" : tailEllipsis(L.path, 60).c_str());
    if (drawPane(L, "##ftpLt", ImVec2(0, 0), to)) goTo(L, to);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##ftpR", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(g_pal.dim, "Android");
    if (ImGui::Button("Naik##r")) { R.path = parentRemote(R.path); R.dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Segarkan##r")) R.dirty = true;
    ImGui::SameLine();
    ImGui::TextUnformatted(tailEllipsis(R.path, 60).c_str());
    if (R.loading) ImGui::TextColored(g_pal.dim, "Memuat...");
    if (drawPane(R, "##ftpRt", ImVec2(0, 0), to)) { R.path = to; R.dirty = true; }
    ImGui::EndChild();
}

// ------------------------------------------------------------------ dialog modal (hapus, ganti nama, folder baru)
void drawModals() {
    static const char* titles[] = {"", "Hapus", "Ganti nama", "Folder baru"};
    if (U.modalOpen) { ImGui::OpenPopup(titles[U.modal]); U.modalOpen = false; }
    if (U.modal == 1 && ImGui::BeginPopupModal("Hapus", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(U.useTrash ? "Pindahkan %d item ke Tempat Sampah?" : "Hapus PERMANEN %d item?", (int)g_modalPaths.size());
        if (ImGui::Button("Hapus", ImVec2(110.f * g_scale, 0))) {
            const std::vector<std::string> paths = g_modalPaths;
            const bool trash = U.useTrash;
            runLocal([paths, trash] {
                if (trash) {
                    std::string err;
                    const bool ok = plat::recycle(paths, err);
                    g_cb.finish(ok, ok ? "Dipindahkan ke Tempat Sampah: " + std::to_string(paths.size()) + " item" : "Gagal: " + err, 0, 0);
                } else {
                    std::vector<fs::path> v;
                    for (auto& s : paths) v.push_back(ng::P(s));
                    ng::removeAll(v, g_cb, g_cancel);
                }
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Batal", ImVec2(110.f * g_scale, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if ((U.modal == 2 || U.modal == 3) && ImGui::BeginPopupModal(titles[U.modal], nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(320.f * g_scale);
        ImGui::InputText("Nama", U.nameBuf, sizeof U.nameBuf);
        if (ImGui::Button("OK", ImVec2(110.f * g_scale, 0))) {
            std::string err;
            const std::string nm = trim(U.nameBuf);
            bool ok = false;
            if (nm.empty() || g_modalPaths.empty()) err = "Nama kosong";
            else if (U.modal == 2) ok = ng::renamePath(ng::P(g_modalPaths[0]), nm, err);
            else ok = ng::makeDir(ng::P(g_modalPaths[0]), nm, err);
            addLog(ok ? "OK     " + std::string(U.modal == 2 ? "nama diganti" : "folder dibuat") : "ERR    " + err);
            if (ok) { std::lock_guard<std::mutex> l(Sh.mu); Sh.refreshAll = true; }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Batal", ImVec2(110.f * g_scale, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------------ tab Pengaturan
void drawSettings() {
    static char devBuf[64];
    static bool devInit = false;
    if (!devInit) { setBuf(devBuf, sizeof devBuf, U.devName); devInit = true; }

    ImGui::TextColored(g_pal.dim, "Tampilan");
    const char* themes[] = {"Ikuti sistem", "Terang", "Gelap"};
    ImGui::SetNextItemWidth(220.f * g_scale);
    if (ImGui::Combo("Tema", &U.theme, themes, 3)) { refreshTheme(); saveSettings(); }
    if (ImGui::Checkbox("Tampilkan file tersembunyi", &U.showHidden)) { U.fm.dirty = true; saveSettings(); }
    if (ImGui::Checkbox("Hapus ke Tempat Sampah", &U.useTrash)) saveSettings();

    ImGui::Separator();
    ImGui::TextColored(g_pal.dim, "Perangkat");
    ImGui::SetNextItemWidth(260.f * g_scale);
    if (ImGui::InputText("Nama perangkat", devBuf, sizeof devBuf)) {
        std::string n = trim(devBuf);
        if (!n.empty()) U.devName = n;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    if (ImGui::Checkbox("Izinkan jelajah FTP dari perangkat lain", &U.allowBrowse)) { if (g_eng) g_eng->setAllowBrowse(U.allowBrowse); saveSettings(); }

    ImGui::Separator();
    ImGui::TextColored(g_pal.dim, "Pembaruan");
    ImGui::Text("Versi saat ini: %s", kVersion);
    if (ImGui::Checkbox("Cek otomatis saat dibuka", &U.autoUpdate)) saveSettings();
    if (ImGui::Checkbox("Sertakan pra-rilis (alpha/beta/rc)", &U.preRelease)) saveSettings();
    if (ImGui::Button("Cek sekarang")) startCheck(true);
    ImGui::SameLine();
    if (ImGui::Button("Halaman rilis")) plat::openUrl(kReleasesPage);

    ImGui::Separator();
    ImGui::TextColored(g_pal.dim, "Riwayat perubahan");
    ImGui::BeginChild("##changelog", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const char* line : kChangelog) {
        if (line[0] == '\0') ImGui::Spacing();
        else ImGui::TextWrapped("%s", line);
    }
    ImGui::EndChild();
}

// ------------------------------------------------------------------ tab Log
void drawLog() {
    std::vector<std::string> copy;
    bool dirty;
    {
        std::lock_guard<std::mutex> l(Sh.mu);
        copy = Sh.logs;
        dirty = Sh.logDirty;
        Sh.logDirty = false;
    }
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (g_mono) ImGui::PushFont(g_mono);
    for (auto& l : copy) {
        const std::string body = l.size() > 11 ? l.substr(11) : l;
        ImVec4 c = g_pal.dim;
        if (body.rfind("ERR", 0) == 0 || body.rfind("GAGAL", 0) == 0) c = g_pal.err;
        else if (body.rfind("OK", 0) == 0) c = g_pal.ok;
        ImGui::TextColored(c, "%s", l.c_str());
    }
    if (g_mono) ImGui::PopFont();
    if (dirty) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

}  // namespace

// ------------------------------------------------------------------ titik masuk (dipanggil main_win.cpp)
void init(float dpiScale) {
    g_scale = dpiScale > 0.5f ? dpiScale : 1.f;
    loadSettings();
    if (U.devName.empty()) U.devName = plat::deviceName();
    if (U.recvDir.empty()) U.recvDir = plat::knownFolder("documents") + "\\Ngirim\\Terima";
    setupStyle(g_scale);
    loadFonts(g_scale);
    refreshTheme();
    g_upd = std::make_unique<ng::Updater>(kVersion, plat::makeUpdaterIO());
    initCallbacks();
    g_eng = std::make_unique<ng::Engine>(g_cb);
    g_mdns = std::make_unique<ng::Mdns>([](const std::string& m) { addLog(m); });
    g_mdns->start();
    addLog(std::string("Ngirim PC v") + kVersion + " dimulai");
}

void frame() {
    // ikuti tema sistem bila dipilih
    static double lastThemePoll = 0;
    const double now = ImGui::GetTime();
    if (U.theme == 0 && now - lastThemePoll > 2.0) {
        lastThemePoll = now;
        if (plat::systemPrefersDark() != U.dark) refreshTheme();
    }
    if (!g_autoChecked && now > 1.5) {
        g_autoChecked = true;
        if (U.autoUpdate) startCheck(false);
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (g_font) ImGui::PushFont(g_font);

    {
        std::lock_guard<std::mutex> l(Sh.mu);
        if (Sh.refreshAll) {
            Sh.refreshAll = false;
            U.fm.dirty = U.ftp[0].dirty = true;
            if (U.paired) U.ftp[1].dirty = true;
        }
    }
    drawUpdateBanner();
    drawProgress();
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Lokal")) { drawLocal(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Wi-Fi", nullptr, U.tabReq == kTabWifi ? ImGuiTabItemFlags_SetSelected : 0)) { drawWifi(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("FTP")) { drawFtp(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Pengaturan")) { drawSettings(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Log")) { drawLog(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    U.tabReq = -1;
    drawModals();

    if (g_font) ImGui::PopFont();
    ImGui::End();
}

void shutdown() {
    saveSettings();
    if (g_mdns && U.listening) g_mdns->unannounce();
    // Thread cek-update bisa masih menunggu jaringan; proses langsung keluar setelah ini, jadi tidak di-join.
    if (g_upd) { g_upd->cancel(); (void)g_upd.release(); }
}

void onDrop(const std::vector<std::string>& paths) {
    for (auto& p : paths) {
        std::error_code ec;
        SendItem it;
        it.path = p;
        it.dir = fs::is_directory(ng::P(p), ec);
        U.sendList.push_back(it);
    }
    addLog("OK     " + std::to_string(paths.size()) + " item ditambahkan ke daftar kirim");
}

}  // namespace app
