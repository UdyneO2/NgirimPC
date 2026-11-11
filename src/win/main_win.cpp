// Titik masuk Windows: jendela Win32, Direct3D 11, dan loop Dear ImGui. Semua logika UI ada di src/app.
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "plat.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

extern HWND g_mainWnd;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_occluded = false;
UINT g_resizeW = 0, g_resizeH = 0;

void createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void cleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof sd);
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // tanpa GPU/driver: pakai renderer perangkat lunak
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (hr != S_OK) return false;
    createRenderTarget();
    return true;
}

void cleanupDevice() {
    cleanupRenderTarget();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

std::string toUtf8(const wchar_t* w, int n) {
    if (n <= 0) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, &s[0], len, nullptr, nullptr);
    return s;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) return 0;
            g_resizeW = (UINT)LOWORD(lp);
            g_resizeH = (UINT)HIWORD(lp);
            return 0;
        case WM_GETMINMAXINFO: {
            UINT dpi = GetDpiForWindow(hwnd);
            MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = (LONG)(820 * dpi / 96);
            mm->ptMinTrackSize.y = (LONG)(560 * dpi / 96);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // matikan menu ALT
            break;
        case WM_DROPFILES: {
            HDROP h = reinterpret_cast<HDROP>(wp);
            UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::string> paths;
            for (UINT i = 0; i < n; i++) {
                UINT len = DragQueryFileW(h, i, nullptr, 0);
                std::wstring w(len + 1, L'\0');
                DragQueryFileW(h, i, &w[0], len + 1);
                paths.push_back(toUtf8(w.c_str(), (int)len));
            }
            DragFinish(h);
            app::onDrop(paths);
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void deleteOldExe() {
    wchar_t self[32768];
    DWORD n = GetModuleFileNameW(nullptr, self, 32768);
    if (n == 0 || n >= 32768) return;
    std::wstring old = std::wstring(self, n) + L".old";
    DeleteFileW(old.c_str());
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    // Satu instans saja (port mDNS dipakai bersama). Setelah update, instans lama butuh waktu untuk keluar.
    const bool updated = wcsstr(GetCommandLineW(), L"--updated") != nullptr;
    HANDLE mutex = nullptr;
    for (int i = 0; i < 40; i++) {
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\NgirimPC.SingleInstance");
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
        if (mutex) CloseHandle(mutex);
        mutex = nullptr;
        if (!updated) break;
        Sleep(250);
    }
    if (!mutex) {
        MessageBoxW(nullptr, L"Ngirim PC sudah berjalan.", L"Ngirim PC", MB_ICONINFORMATION | MB_OK);
        return 0;
    }
    deleteOldExe();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))  /* IDC_ARROW */;
    wc.lpszClassName = L"NgirimPCWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Ngirim PC", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180,
                              760, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    g_mainWnd = hwnd;

    const UINT dpi = GetDpiForWindow(hwnd);
    const float scale = dpi / 96.f;
    {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int w = (int)(1180 * scale), h = (int)(760 * scale);
        if (w > wa.right - wa.left) w = wa.right - wa.left;
        if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
        int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 2;
        SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!createDevice(hwnd)) {
        cleanupDevice();
        UnregisterClassW(wc.lpszClassName, inst);
        MessageBoxW(nullptr, L"Direct3D 11 tidak tersedia di komputer ini.", L"Ngirim PC", MB_ICONERROR | MB_OK);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    DragAcceptFiles(hwnd, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    app::init(scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_occluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(20);
            continue;
        }
        g_occluded = false;
        if (g_resizeW != 0 && g_resizeH != 0) {
            cleanupRenderTarget();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app::frame();
        ImGui::Render();

        const float clear[4] = {0.12f, 0.12f, 0.12f, 1.0f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_swap->Present(1, 0);
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    app::shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    // Thread latar (cek update, pairing) tidak ditunggu: keluar langsung tanpa destruktor global.
    ExitProcess(0);
    return 0;
}
