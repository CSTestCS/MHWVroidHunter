// Test harness: a stand-in for MonsterHunterWorld.exe. It renders with DirectX 11, loads
// dinput8.dll from its own folder like the game does, calls DirectInput8Create, presents frames
// and saves a screenshot of the back buffer so the overlay can be checked without the game.
// Build as MonsterHunterWorld.exe and run under Windows or Wine:
//   MonsterHunterWorld.exe <frames> <screenshot.bmp> [file that must exist after start-up]
#include <windows.h>
#include <d3d11.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}

int main(int argc, char** argv) {
    int frames = argc > 1 ? std::atoi(argv[1]) : 120;
    const char* shot = argc > 2 ? argv[2] : "screenshot.bmp";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MT FRAMEWORK";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MONSTER HUNTER: WORLD (fake)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 1280, 720,
                                nullptr, nullptr, wc.hInstance, nullptr);
    SetForegroundWindow(hwnd);

    // like the game: dinput8.dll is resolved from the exe folder first
    HMODULE di = LoadLibraryW(L"dinput8.dll");
    typedef HRESULT(WINAPI * CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    CreateFn create = di ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(di, "DirectInput8Create"))) : nullptr;
    std::printf("dinput8 loaded: %s, DirectInput8Create: %s\n", di ? "yes" : "no", create ? "yes" : "no");
    if (create) {
        static const GUID IID_IDirectInput8W_ = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
        void* out = nullptr;
        HRESULT hr = create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8W_, &out, nullptr);
        std::printf("DirectInput8Create hr=0x%08lx\n", static_cast<unsigned long>(hr));
        // optional: check that a converted model file exists once DirectInput8Create returned
        if (argc > 3) std::printf("model ready after start-up wait: %s\n", GetFileAttributesA(argv[3]) != INVALID_FILE_ATTRIBUTES ? "yes" : "no");
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 1280;
    sd.BufferDesc.Height = 720;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL; // keeps the presented image readable for the screenshot
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                             nullptr, &ctx))) {
        std::printf("no d3d11\n");
        return 1;
    }
    ID3D11Texture2D* back = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    ID3D11RenderTargetView* rtv = nullptr;
    dev->CreateRenderTargetView(back, nullptr, &rtv);
    for (int f = 0; f < frames; ++f) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const float c[4] = {0.15f, 0.35f, 0.25f, 1.0f};
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, c);
        if (f == frames - 1) {
            // capture the frame including anything the overlay drew in Present: draw, present,
            // then read back buffer 0 which still holds the presented image with DISCARD on wine.
        }
        sc->Present(1, 0);
        Sleep(16);
    }
    // read the current back buffer after one more overlay pass
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const float c[4] = {0.15f, 0.35f, 0.25f, 1.0f};
    ctx->ClearRenderTargetView(rtv, c);
    // Present draws the overlay into the back buffer before flipping; copy happens via a second
    // swap chain buffer read below.
    sc->Present(0, 0);
    ID3D11Texture2D* cur = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&cur));
    D3D11_TEXTURE2D_DESC td;
    cur->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    dev->CreateTexture2D(&td, nullptr, &staging);
    ctx->CopyResource(staging, cur);
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        FILE* fp = std::fopen(shot, "wb");
        int w = int(td.Width), h = int(td.Height);
        BITMAPFILEHEADER bf{};
        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(bi);
        bi.biWidth = w;
        bi.biHeight = -h;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bf.bfType = 0x4D42;
        bf.bfOffBits = sizeof(bf) + sizeof(bi);
        bf.bfSize = bf.bfOffBits + w * h * 4;
        std::fwrite(&bf, sizeof(bf), 1, fp);
        std::fwrite(&bi, sizeof(bi), 1, fp);
        std::vector<unsigned char> row(size_t(w) * 4);
        for (int y = 0; y < h; ++y) {
            const unsigned char* src = static_cast<const unsigned char*>(map.pData) + size_t(y) * map.RowPitch;
            for (int x = 0; x < w; ++x) {
                row[size_t(x) * 4 + 0] = src[x * 4 + 2];
                row[size_t(x) * 4 + 1] = src[x * 4 + 1];
                row[size_t(x) * 4 + 2] = src[x * 4 + 0];
                row[size_t(x) * 4 + 3] = 255;
            }
            std::fwrite(row.data(), 1, row.size(), fp);
        }
        std::fclose(fp);
        ctx->Unmap(staging, 0);
        std::printf("saved %s\n", shot);
    }
    return 0;
}
