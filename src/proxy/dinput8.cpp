// dinput8.dll proxy: forwards DirectInput8Create to the real system DLL and loads
// nativePC/plugins/MHWVroidHunter.dll into the game.
// (If Stracker's Loader is installed it already provides dinput8.dll and loads our plugin.)
#include <windows.h>

#include <string>

// Marker the installer looks for to recognise its own proxy.
extern "C" const char MHWVroidHunterProxyMarker[] = "MHWVroidHunter-dinput8-proxy";

namespace {

HMODULE g_real = nullptr;

using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
CreateFn g_create = nullptr;

void loadReal() {
    if (g_real) return;
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring path = std::wstring(sys) + L"\\dinput8.dll";
    g_real = LoadLibraryW(path.c_str());
    if (g_real) g_create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(g_real, "DirectInput8Create")));
}

DWORD WINAPI loadPlugin(LPVOID) {
    wchar_t exe[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring plugin = dir + L"\\nativePC\\plugins\\MHWVroidHunter.dll";
    if (GetFileAttributesW(plugin.c_str()) != INVALID_FILE_ATTRIBUTES) LoadLibraryW(plugin.c_str());
    return 0;
}

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    loadReal();
    if (!g_create) return E_FAIL;
    return g_create(inst, version, riid, out, outer);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // LoadLibrary must not run inside DllMain; the thread starts once the loader lock is free.
        HANDLE t = CreateThread(nullptr, 0, loadPlugin, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
