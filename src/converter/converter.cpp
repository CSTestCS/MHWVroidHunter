// VRoidHunterConverter.exe - converts the model from the drop folder and installs it.
//
// Lives in <game>/nativePC/plugins/MHWVroidHunter/. The plugin starts it when the game starts
// (--auto) and when "Apply" / "Remove" is pressed in the F7 menu. Running in its own process
// means the conversion can never crash or freeze the game.
//
//   VRoidHunterConverter.exe --auto|--apply|--remove [<game folder>] [--hidden]
//
// Progress is written to converter_status.txt next to the exe; the last line is
// "#DONE 1 <message>" on success or "#DONE 0 <message>" on failure.
#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "autoconvert.h"
#include "fsutil.h"
#include "procutil.h"
#include "version.h"

namespace {

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr UINT_PTR TIMER_CLOSE = 1;

DWORD g_mainThread = 0;
HWND g_wnd = nullptr, g_text = nullptr;
bool g_hidden = false;
std::string g_statusPath;
std::mutex g_statusMutex;
std::wstring g_title = L"VRoid Hunter";
std::string g_runId = "manual";
std::string g_lastStep = "starting";

// Last words if something goes badly wrong (e.g. unreadable game data): the plugin shows them.
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    char b[300];
    std::snprintf(b, sizeof(b), "#DONE 0 The converter crashed (exception 0x%08lX) while: %s",
                  static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode), g_lastStep.c_str());
    std::FILE* f = _wfopen(vh::u8(g_statusPath).wstring().c_str(), L"ab");
    if (f) {
        std::fprintf(f, "%s\n", b);
        std::fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

std::wstring w(const std::string& s) { return vh::u8(s).wstring(); }

void writeStatus(const std::string& line, bool truncate = false) {
    std::lock_guard<std::mutex> lk(g_statusMutex);
    std::FILE* f = _wfopen(w(g_statusPath).c_str(), truncate ? L"wb" : L"ab");
    if (!f) return;
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
}

void post(UINT msg, const std::string& text) {
    auto* copy = new std::wstring(w(text));
    PostThreadMessageW(g_mainThread, msg, 0, reinterpret_cast<LPARAM>(copy));
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_TIMER && wp == TIMER_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    if (m == WM_CLOSE) return 0; // closes by itself when finished
    return DefWindowProcW(h, m, wp, lp);
}

void ensureWindow() {
    if (g_wnd || g_hidden) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"VRoidHunterConverter";
    RegisterClassExW(&wc);
    RECT r{0, 0, 560, 120};
    AdjustWindowRect(&r, WS_CAPTION, FALSE);
    int cx = r.right - r.left, cy = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - cx) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - cy) / 3;
    // NOACTIVATE: never steal focus from the game (that could minimise a fullscreen game)
    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, g_title.c_str(), WS_CAPTION | WS_POPUP,
                            x, y, cx, cy, nullptr, nullptr, inst, nullptr);
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    g_text = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 14, 528, 92, g_wnd, nullptr, inst, nullptr);
    SendMessageW(g_text, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
}

void setText(const std::wstring& line) {
    if (!g_text) return;
    std::wstring t = L"Preparing your VRoid model for Monster Hunter: World...\r\nThe first time can take a few minutes.\r\n\r\n" + line;
    SetWindowTextW(g_text, t.c_str());
}

std::string gameDirFromExe() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    std::filesystem::path p(std::wstring(buf, n));
    // <game>/nativePC/plugins/MHWVroidHunter/VRoidHunterConverter.exe
    return vh::toU8(p.parent_path().parent_path().parent_path().parent_path());
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    vh::JobMode mode = vh::JobMode::Auto;
    std::string game;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--auto") mode = vh::JobMode::Auto;
        else if (a == L"--apply") mode = vh::JobMode::Apply;
        else if (a == L"--remove") mode = vh::JobMode::Remove;
        else if (a == L"--hidden") g_hidden = true;
        else if (a == L"--run" && i + 1 < argc) g_runId = vh::toU8(std::filesystem::path(argv[++i]));
        else game = vh::toU8(std::filesystem::path(a));
    }
    if (game.empty()) game = gameDirFromExe();
    const std::string data = vh::pluginDataDir(game);
    try {
        vh::makeDirs(data);
    } catch (...) {
    }
    g_statusPath = vh::join(data, "converter_status.txt");
    g_mainThread = GetCurrentThreadId();
    // At start-up the game waits for us, so run at full speed; in-game work stays in the background.
    SetPriorityClass(GetCurrentProcess(), mode == vh::JobMode::Auto ? NORMAL_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS);

    // one conversion at a time
    HANDLE mtx = CreateMutexW(nullptr, FALSE, L"Local\\VRoidHunterConverter");
    if (mtx) WaitForSingleObject(mtx, INFINITE);

    writeStatus(std::string("#VERSION " VH_VERSION " RUN ") + g_runId, true);
    writeStatus("Game folder: " + game);
    SetUnhandledExceptionFilter(crashFilter);
    // make sure the thread has a message queue before the worker posts to it
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    std::atomic<bool> ok{false};
    std::thread worker([&] {
        vh::JobResult r;
        try {
            r = vh::runJob(game, mode, [](const std::string& m) {
            g_lastStep = m;
            writeStatus(m);
            post(WM_APP_LOG, m);
        });
        } catch (const std::exception& e) {
            r.ok = false;
            r.message = e.what();
        }
        ok = r.ok;
        writeStatus(std::string("#DONE ") + (r.ok ? "1 " : "0 ") + r.message);
        post(WM_APP_DONE, r.message);
    });

    int exitCode = 0;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && (msg.message == WM_APP_LOG || msg.message == WM_APP_DONE)) {
            auto* text = reinterpret_cast<std::wstring*>(msg.lParam);
            if (msg.message == WM_APP_LOG) {
                ensureWindow();
                setText(*text);
            } else {
                exitCode = ok ? 0 : 1;
                if (g_wnd) {
                    setText((ok ? L"Done! " : L"Could not convert the model: ") + *text);
                    SetTimer(g_wnd, TIMER_CLOSE, ok ? 2500 : 12000, nullptr);
                } else {
                    PostQuitMessage(0);
                }
            }
            delete text;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    worker.join();
    if (mtx) {
        ReleaseMutex(mtx);
        CloseHandle(mtx);
    }
    return exitCode;
}
