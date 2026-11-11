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
#include "../core/updater_core.h"
#include "../win/plat.h"

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
    "v1.0",
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
