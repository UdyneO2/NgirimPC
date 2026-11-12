// Implementasi platform Windows 10/11: dialog file, shell, lokasi, WinHTTP, dan penggantian exe saat update.
#include "plat.h"

#include <windows.h>
#include <initguid.h>  // definisikan GUID (FOLDERID_*, CLSID_*) di unit ini
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

#ifndef NGIRIMPC_VERSION
#define NGIRIMPC_VERSION "0.0"
#endif

HWND g_mainWnd = nullptr;

namespace {

std::wstring W(const std::string& u8) {
    if (u8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), &w[0], n);
    return w;
}

std::string A(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::vector<std::string> pickItems(bool folders, bool multi, const std::string& title) {
    std::vector<std::string> out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg)
        return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (folders) opts |= FOS_PICKFOLDERS;
    if (multi) opts |= FOS_ALLOWMULTISELECT;
    dlg->SetOptions(opts);
    if (!title.empty()) dlg->SetTitle(W(title).c_str());
    if (SUCCEEDED(dlg->Show(g_mainWnd))) {
        IShellItemArray* arr = nullptr;
        if (SUCCEEDED(dlg->GetResults(&arr)) && arr) {
            DWORD n = 0;
            arr->GetCount(&n);
            for (DWORD i = 0; i < n; i++) {
                IShellItem* it = nullptr;
                if (SUCCEEDED(arr->GetItemAt(i, &it)) && it) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                        out.push_back(A(p));
                        CoTaskMemFree(p);
                    }
                    it->Release();
                }
            }
            arr->Release();
        }
    }
    dlg->Release();
    return out;
}

std::string folderById(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = A(p);
    if (p) CoTaskMemFree(p);
    return r;
}

// ---- WinHTTP
struct Http {
    HINTERNET ses = nullptr, con = nullptr, req = nullptr;
    ~Http() {
        if (req) WinHttpCloseHandle(req);
        if (con) WinHttpCloseHandle(con);
        if (ses) WinHttpCloseHandle(ses);
    }
};

bool openGet(const std::string& url, Http& h, std::string& err) {
    std::wstring wurl = W(url);
    URL_COMPONENTSW uc;
    ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) { err = "URL tidak valid"; return false; }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) { err = "Hanya HTTPS yang diizinkan"; return false; }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path;
    if (uc.lpszUrlPath && uc.dwUrlPathLength) path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    std::wstring ua = L"NgirimPC/" + W(NGIRIMPC_VERSION);
    h.ses = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.ses) { err = "WinHTTP gagal dibuka"; return false; }
    WinHttpSetTimeouts(h.ses, 10000, 10000, 30000, 30000);
    h.con = WinHttpConnect(h.ses, host.c_str(), uc.nPort, 0);
    if (!h.con) { err = "Tidak bisa terhubung ke server"; return false; }
    DWORD flags = WINHTTP_FLAG_SECURE;
    h.req = WinHttpOpenRequest(h.con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h.req) { err = "Permintaan HTTP gagal dibuat"; return false; }
    const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream, */*\r\n";
    WinHttpAddRequestHeaders(h.req, hdr, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(h.req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(h.req, nullptr)) {
        err = "Tidak ada koneksi internet (kode " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(h.req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &code, &sz, WINHTTP_NO_HEADER_INDEX);
    if (code != 200) {
        err = "HTTP " + std::to_string(code);
        if (code == 403 || code == 429) err += " (batas permintaan GitHub, coba lagi nanti)";
        return false;
    }
    return true;
}

}  // namespace

namespace plat {

std::vector<std::string> pickFiles() { return pickItems(false, true, "Pilih file untuk dikirim"); }

std::string pickFolder(const std::string& title) {
    auto v = pickItems(true, false, title);
    return v.empty() ? "" : v[0];
}

void openInExplorer(const std::string& path) {
    DWORD a = GetFileAttributesW(W(path).c_str());
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring args = L"/select,\"" + W(path) + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", W(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void openPath(const std::string& path) {
    ShellExecuteW(nullptr, L"open", W(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openUrl(const std::string& url) {
    ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool recycle(const std::vector<std::string>& paths, std::string& err) {
    std::wstring from;
    for (auto& p : paths) {
        from += W(p);
        from.push_back(L'\0');
    }
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof op);
    op.hwnd = g_mainWnd;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    int r = SHFileOperationW(&op);
    if (r != 0 || op.fAnyOperationsAborted) {
        err = "Gagal memindahkan ke Tempat Sampah (kode " + std::to_string(r) + ")";
        return false;
    }
    return true;
}

std::string knownFolder(const std::string& which) {
    std::string r;
    if (which == "desktop") r = folderById(FOLDERID_Desktop);
    else if (which == "documents") r = folderById(FOLDERID_Documents);
    else if (which == "downloads") r = folderById(FOLDERID_Downloads);
    else if (which == "pictures") r = folderById(FOLDERID_Pictures);
    else if (which == "videos") r = folderById(FOLDERID_Videos);
    else if (which == "music") r = folderById(FOLDERID_Music);
    else r = folderById(FOLDERID_Profile);
    if (r.empty()) {
        const char* up = std::getenv("USERPROFILE");
        r = up ? up : "C:\\";
    }
    return r;
}

std::vector<std::string> drives() {
    std::vector<std::string> out;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
        UINT t = GetDriveTypeW(root);
        if (t == DRIVE_FIXED || t == DRIVE_REMOVABLE || t == DRIVE_REMOTE || t == DRIVE_RAMDISK) out.push_back(A(root));
    }
    return out;
}

std::string deviceName() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buf, &n)) return A(std::wstring(buf, n));
    return "PC";
}

std::string dataDir() {
    std::string base = folderById(FOLDERID_RoamingAppData);
    if (base.empty()) base = tempDir();
    std::string d = base + "\\NgirimPC";
    CreateDirectoryW(W(d).c_str(), nullptr);
    return d;
}

bool fileExists(const std::string& path) { return GetFileAttributesW(W(path).c_str()) != INVALID_FILE_ATTRIBUTES; }

bool resourceData(int id, const void*& data, size_t& size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return false;
    HGLOBAL h = LoadResource(nullptr, r);
    if (!h) return false;
    data = LockResource(h);
    size = SizeofResource(nullptr, r);
    return data != nullptr && size > 0;
}

std::string systemLanguage() {
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) <= 0) return "";
    std::string out;
    for (const wchar_t* c = buf; *c && *c != L'-' && *c != L'_'; c++) out += (char)(*c < 128 ? *c : '?');
    for (auto& ch : out) ch = (char)std::tolower((unsigned char)ch);
    return out;
}

bool systemPrefersDark() {
    DWORD v = 1, sz = sizeof v;
    LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                             L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return r == ERROR_SUCCESS && v == 0;
}

bool httpGet(const std::string& url, std::string& body, std::string& err) {
    Http h;
    if (!openGet(url, h, err)) return false;
    body.clear();
    char buf[16384];
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(h.req, buf, sizeof buf, &got)) { err = "Gagal membaca respons"; return false; }
        if (got == 0) break;
        body.append(buf, got);
        if (body.size() > (64u << 20)) { err = "Respons terlalu besar"; return false; }
    }
    return true;
}

bool httpDownload(const std::string& url, const std::string& dest,
                  const std::function<void(int64_t, int64_t)>& progress, const std::atomic<bool>& cancel,
                  std::string& err) {
    Http h;
    if (!openGet(url, h, err)) return false;
    DWORD len = 0, sz = sizeof len;
    int64_t total = 0;
    if (WinHttpQueryHeaders(h.req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &len, &sz, WINHTTP_NO_HEADER_INDEX))
        total = len;
    FILE* f = nullptr;
    if (_wfopen_s(&f, W(dest).c_str(), L"wb") != 0 || !f) { err = "Tidak bisa menulis " + dest; return false; }
    std::vector<char> buf(1 << 16);
    int64_t done = 0;
    bool ok = true;
    for (;;) {
        if (cancel) { err = "Dibatalkan"; ok = false; break; }
        DWORD got = 0;
        if (!WinHttpReadData(h.req, buf.data(), (DWORD)buf.size(), &got)) { err = "Unduhan terputus"; ok = false; break; }
        if (got == 0) break;
        if (fwrite(buf.data(), 1, got, f) != got) { err = "Disk penuh atau tidak bisa ditulis"; ok = false; break; }
        done += got;
        if (progress) progress(done, total);
    }
    fclose(f);
    if (!ok) DeleteFileW(W(dest).c_str());
    return ok;
}

std::string tempDir() {
    wchar_t buf[MAX_PATH + 2];
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    std::string d = A(std::wstring(buf, n));
    if (!d.empty() && (d.back() == '\\' || d.back() == '/')) d.pop_back();
    d += "\\NgirimPC-update";
    CreateDirectoryW(W(d).c_str(), nullptr);
    return d;
}

bool applyUpdate(const std::string& newExe, std::string& err) {
    wchar_t self[32768];
    DWORD n = GetModuleFileNameW(nullptr, self, 32768);
    if (n == 0 || n >= 32768) { err = "Lokasi aplikasi tidak diketahui"; return false; }
    std::wstring cur(self, n), old = cur + L".old", src = W(newExe);
    DeleteFileW(old.c_str());
    // exe yang sedang berjalan boleh diganti namanya (tidak boleh ditimpa)
    if (!MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "Folder aplikasi tidak bisa ditulis. Pindahkan NgirimPC.exe ke folder biasa (mis. Dokumen). Kode " +
              std::to_string(GetLastError());
        return false;
    }
    if (!CopyFileW(src.c_str(), cur.c_str(), FALSE)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);
        err = "Gagal memasang versi baru (kode " + std::to_string(e) + ")";
        return false;
    }
    std::wstring cmd = L"\"" + cur + L"\" --updated";
    std::vector<wchar_t> cb(cmd.begin(), cmd.end());
    cb.push_back(L'\0');
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(cur.c_str(), cb.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        DWORD e = GetLastError();
        DeleteFileW(cur.c_str());
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);
        err = "Gagal menjalankan versi baru (kode " + std::to_string(e) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(src.c_str());
    return true;
}

void requestQuit() {
    if (g_mainWnd) PostMessageW(g_mainWnd, WM_CLOSE, 0, 0);
}

}  // namespace plat
