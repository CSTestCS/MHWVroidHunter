// MHW VRoid Hunter plugin entry point.
//
// Loaded into MonsterHunterWorld.exe either by our dinput8.dll proxy or by Stracker's Loader
// (both load DLLs from nativePC/plugins). It never touches game memory: the model swap is done
// with loose files under nativePC, and this DLL only provides the settings menu that builds them.
#include <windows.h>
#include <tlhelp32.h>

#include <chrono>
#include <thread>

#include "app.h"
#include "fsutil.h"
#include "autoconvert.h"
#include "models.h"

namespace {

std::string moduleDir(HMODULE m) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(m, buf, MAX_PATH * 4);
    std::filesystem::path p(std::wstring(buf, n));
    return vh::toU8(p.parent_path());
}

bool isGameProcess() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    std::wstring exe = std::filesystem::path(std::wstring(buf, n)).filename().wstring();
    return _wcsicmp(exe.c_str(), L"MonsterHunterWorld.exe") == 0;
}

bool gameWindowFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool gameWindowExists() {
    struct Ctx { DWORD pid; bool found; } ctx{GetCurrentProcessId(), false};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid && IsWindowVisible(h)) {
            c->found = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

void mainThread() {
    using namespace vhp;
    SetEvent(startupBegunEvent());
    App& a = app();
    a.gameDir = moduleDir(nullptr);
    a.nativePC = vh::join(a.gameDir, "nativePC");
    a.dataDir = vh::join(a.nativePC, "plugins/MHWVroidHunter");
    a.settingsPath = vh::join(a.dataDir, "settings.ini");
    try {
        vh::makeDirs(a.dataDir);
    } catch (...) {
    }
    {
        std::lock_guard<std::mutex> lk(a.mutex);
        a.settings = vh::Settings::load(a.settingsPath);
        if (!vh::exists(a.settingsPath)) a.saveSettings();
    }
    logLine("---- MHW VRoid Hunter started ----");

    // Nothing is converted at start-up (that only happens when "Apply" is pressed). Just report
    // whether the installed model matches the model in the VRoidHunter folder.
    try {
        vh::ensureModelsFolder(a.gameDir);
    } catch (...) {
    }
    a.refreshModels();
    {
        bool current = vh::modelUpToDate(a.gameDir);
        std::string model;
        bool enabled;
        {
            std::lock_guard<std::mutex> lk(a.mutex);
            model = a.activeModel;
            enabled = a.settings.enabled;
        }
        if (!enabled) a.setStatus("Mod is turned off.");
        else if (model.empty()) a.setStatus("No .vrm file yet: put one in the VRoidHunter folder, then press F7 and Apply.");
        else if (current) a.setStatus("VRoid model installed: " + model);
        else a.setStatus("Model \"" + model + "\" is not installed yet: press F7, then Apply.");
    }
    SetEvent(startupDoneEvent());

    // Let the game create its window and DirectX device first; creating our temporary device at
    // the same moment as the game creates its own can make the game's fail.
    for (int i = 0; i < 1200 && !gameWindowExists(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const bool hooked = installOverlay();
    // Test hook: start with the menu open (used by the automated overlay test harness).
    if (GetEnvironmentVariableW(L"VH_OPEN_MENU_ON_START", nullptr, 0) > 0) a.menuVisible = true;
    if (GetEnvironmentVariableW(L"VH_APPLY_ON_START", nullptr, 0) > 0) a.startApply();
    bool wasDown = false;
    const auto start = std::chrono::steady_clock::now();
    bool announcedFallback = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int key;
        {
            std::lock_guard<std::mutex> lk(a.mutex);
            key = a.settings.hotkey > 0 ? a.settings.hotkey : VK_F7;
        }
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !wasDown && (gameWindowFocused())) {
            if (a.overlayActive) {
                a.menuVisible = !a.menuVisible;
            } else {
                // DirectX 12 mode or no hook: use the separate window
                toggleStandaloneWindow();
            }
        }
        wasDown = down;
        static int tick = 0;
        if (a.menuVisible && !a.busy && ++tick % 100 == 0) a.refreshModels(); // pick up newly dropped files
        if (!hooked && !announcedFallback) {
            announcedFallback = true;
            logLine("Press the hotkey in game to open the settings window.");
        }
        (void)start;
    }
}

} // namespace

// Called by our dinput8.dll loader from the game's first DirectInput8Create call, so the game
// waits until a newly dropped model has been converted. Returns TRUE if the work finished.
extern "C" __declspec(dllexport) BOOL WINAPI VH_WaitForStartupWork(DWORD timeoutMs) {
    // never block the game if our start-up thread could not even start
    if (WaitForSingleObject(vhp::startupBegunEvent(), 10000) != WAIT_OBJECT_0) return FALSE;
    return WaitForSingleObject(vhp::startupDoneEvent(), timeoutMs) == WAIT_OBJECT_0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        if (!isGameProcess()) return TRUE; // loaded by something else (e.g. the installer): stay idle
        // Only one instance even if both our proxy and Stracker's Loader load us.
        wchar_t name[64];
        wsprintfW(name, L"MHWVroidHunter_%lu", GetCurrentProcessId());
        HANDLE mtx = CreateMutexW(nullptr, TRUE, name);
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mtx);
            return TRUE;
        }
        vhp::startupDoneEvent(); // create them before anybody can wait on them
        vhp::startupBegunEvent();
        CloseHandle(CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            mainThread();
            return 0;
        }, nullptr, 0, nullptr));
    }
    return TRUE;
}
