// In-game settings menu.
//
// DirectX 11 mode: IDXGISwapChain::Present is hooked (vtable address taken from a throw-away swap
// chain) and the ImGui menu is drawn into the game's back buffer.
// DirectX 12 mode (or if hooking fails): the same menu opens in a small always-on-top window.
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <MinHook.h>

#include <thread>

#include "app.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace vhp {

std::mutex& imguiMutex() {
    static std::mutex m;
    return m;
}

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_origPresent = nullptr;
ResizeBuffersFn g_origResize = nullptr;
WNDPROC g_origWndProc = nullptr;
HWND g_gameWindow = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ImGuiContext* g_overlayCtx = nullptr;
bool g_initTried = false;
bool g_isDx11 = false;

void releaseRtv() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

void createRtv(IDXGISwapChain* sc) {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

bool isInputMessage(UINT msg) {
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_INPUT;
}

LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (app().menuVisible && g_overlayCtx) {
        std::lock_guard<std::mutex> lk(imguiMutex());
        ImGui::SetCurrentContext(g_overlayCtx);
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        ImGuiIO& io = ImGui::GetIO();
        if (isInputMessage(msg) && (io.WantCaptureMouse || io.WantCaptureKeyboard) && msg != WM_INPUT) return 0;
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

void initOverlay(IDXGISwapChain* sc) {
    g_initTried = true;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device))) || !g_device) {
        logLine("Game is not using DirectX 11; the settings menu will open in its own window.");
        return; // DirectX 12
    }
    g_device->GetImmediateContext(&g_context);
    DXGI_SWAP_CHAIN_DESC desc{};
    sc->GetDesc(&desc);
    g_gameWindow = desc.OutputWindow;

    std::lock_guard<std::mutex> lk(imguiMutex());
    IMGUI_CHECKVERSION();
    g_overlayCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_overlayCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    setupFonts(18.0f);
    ImGui_ImplWin32_Init(g_gameWindow);
    ImGui_ImplDX11_Init(g_device, g_context);
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hookedWndProc)));
    g_isDx11 = true;
    app().overlayActive = true;
    logLine("DirectX 11 overlay ready.");
}

HRESULT STDMETHODCALLTYPE hookedPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_initTried) initOverlay(sc);
    if (g_isDx11) {
        std::lock_guard<std::mutex> lk(imguiMutex());
        ImGui::SetCurrentContext(g_overlayCtx);
        if (!g_rtv) createRtv(sc);
        if (g_rtv) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            const bool visible = app().menuVisible;
            ImGui::GetIO().MouseDrawCursor = visible;
            if (visible) drawSettingsUI(false);
            else drawStartupHint();
            ImGui::Render();
            g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }
    return g_origPresent(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE hookedResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    {
        std::lock_guard<std::mutex> lk(imguiMutex());
        releaseRtv();
    }
    return g_origResize(sc, count, w, h, fmt, flags);
}

// ---------------------------------------------------------------- standalone window
std::atomic<bool> g_standaloneOpen{false};
std::atomic<bool> g_standaloneClose{false};

LRESULT CALLBACK standaloneProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    if (msg == WM_CLOSE) {
        g_standaloneClose = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void standaloneThread() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = standaloneProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MHWVroidHunterSettings";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"VRoid Hunter settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                80, 80, 620, 560, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                             nullptr, &ctx))) {
        logLine("Could not create the settings window.");
        DestroyWindow(hwnd);
        g_standaloneOpen = false;
        return;
    }
    ID3D11RenderTargetView* rtv = nullptr;
    {
        ID3D11Texture2D* back = nullptr;
        sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
        dev->CreateRenderTargetView(back, nullptr, &rtv);
        back->Release();
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    ImGuiContext* ctxImgui;
    {
        std::lock_guard<std::mutex> lk(imguiMutex());
        ctxImgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctxImgui);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        setupFonts(17.0f);
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(dev, ctx);
    }
    g_standaloneClose = false;
    while (!g_standaloneClose) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            std::lock_guard<std::mutex> lk(imguiMutex());
            ImGui::SetCurrentContext(ctxImgui);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        {
            std::lock_guard<std::mutex> lk(imguiMutex());
            ImGui::SetCurrentContext(ctxImgui);
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("##standalone", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            drawSettingsUI(true);
            ImGui::End();
            ImGui::Render();
            const float clear[4] = {0.08f, 0.08f, 0.1f, 1.0f};
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->ClearRenderTargetView(rtv, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        sc->Present(1, 0);
    }
    {
        std::lock_guard<std::mutex> lk(imguiMutex());
        ImGui::SetCurrentContext(ctxImgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(ctxImgui);
        if (g_overlayCtx) ImGui::SetCurrentContext(g_overlayCtx);
    }
    rtv->Release();
    sc->Release();
    ctx->Release();
    dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_standaloneOpen = false;
}

} // namespace

void toggleStandaloneWindow() {
    if (g_standaloneOpen) {
        g_standaloneClose = true;
        return;
    }
    g_standaloneOpen = true;
    std::thread(standaloneThread).detach();
}

bool installOverlay() {
    // Dummy device + swap chain on a hidden window to read the DXGI vtable.
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MHWVroidHunterDummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc,
                                               &dev, nullptr, &ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                           nullptr, &ctx);
    }
    bool ok = false;
    if (SUCCEEDED(hr)) {
        void** vtable = *reinterpret_cast<void***>(sc);
        void* present = vtable[8];
        void* resize = vtable[13];
        MH_STATUS init = MH_Initialize();
        if (init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED) {
            ok = MH_CreateHook(present, reinterpret_cast<void*>(&hookedPresent), reinterpret_cast<void**>(&g_origPresent)) == MH_OK &&
                 MH_CreateHook(resize, reinterpret_cast<void*>(&hookedResizeBuffers), reinterpret_cast<void**>(&g_origResize)) == MH_OK &&
                 MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
        }
        sc->Release();
        dev->Release();
        ctx->Release();
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    logLine(ok ? "Present hook installed." : "Could not hook DirectX; the settings menu will open in its own window.");
    return ok;
}

} // namespace vhp
