// MHWVroidHunterSetup.exe - one-click installer.
//
// * finds Monster Hunter: World in the Steam libraries (or lets the user browse)
// * copies the plugin to <game>/nativePC/plugins and, unless Stracker's Loader is present,
//   our small dinput8.dll loader next to the game exe
// * optionally converts a VRM right away, so the model is ready the first time the game starts
// * uninstall removes every file the mod created and restores the normal hunter
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <atomic>
#include <mutex>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "builder.h"
#include "fsutil.h"
#include "settings.h"

namespace {

enum { IDC_PATH = 101, IDC_BROWSE, IDC_INSTALL, IDC_UNINSTALL, IDC_CHOOSE, IDC_LOG, IDC_ARMOR, IDR_PLUGIN = 201, IDR_PROXY = 202 };
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr wchar_t kGameExe[] = L"MonsterHunterWorld.exe";

HWND g_wnd, g_path, g_log, g_install, g_uninstall, g_choose;
std::atomic<bool> g_busy{false};

std::wstring w(const std::string& s) { return vh::u8(s).wstring(); }
std::string u(const std::wstring& s) { return vh::toU8(std::filesystem::path(s)); }

std::wstring g_silentLog; // command line mode: collected instead of shown
std::mutex g_silentMutex;

void appendLog(const std::wstring& line) {
    if (!g_wnd) {
        std::lock_guard<std::mutex> lk(g_silentMutex);
        g_silentLog += line + L"\r\n";
        return;
    }
    auto* copy = new std::wstring(line + L"\r\n");
    PostMessageW(g_wnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(copy));
}
void appendLog(const std::string& line) { appendLog(w(line)); }

std::wstring readRegString(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return buf;
    return L"";
}

std::vector<std::wstring> steamLibraries() {
    std::vector<std::wstring> libs;
    std::wstring steam = readRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) steam = readRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (steam.empty()) steam = L"C:\\Program Files (x86)\\Steam";
    libs.push_back(steam);
    try {
        auto raw = vh::readFile(u(steam + L"\\steamapps\\libraryfolders.vdf"));
        std::string text(raw.begin(), raw.end());
        std::regex re("\"path\"\\s*\"([^\"]+)\"");
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
            std::string p = (*it)[1].str();
            std::string unescaped;
            for (size_t i = 0; i < p.size(); ++i) {
                if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') ++i;
                unescaped.push_back(p[i]);
            }
            libs.push_back(w(unescaped));
        }
    } catch (...) {
    }
    return libs;
}

std::wstring findGame() {
    for (auto& lib : steamLibraries()) {
        std::wstring dir = lib + L"\\steamapps\\common\\Monster Hunter World";
        if (GetFileAttributesW((dir + L"\\" + kGameExe).c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
    }
    return L"";
}

bool gameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, kGameExe) == 0) found = true;
    CloseHandle(snap);
    return found;
}

std::vector<uint8_t> resourceBytes(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!r) throw std::runtime_error("installer is damaged (missing resource)");
    HGLOBAL g = LoadResource(nullptr, r);
    const uint8_t* p = static_cast<const uint8_t*>(LockResource(g));
    return std::vector<uint8_t>(p, p + SizeofResource(nullptr, r));
}

bool isOurProxy(const std::string& path) {
    try {
        auto bytes = vh::readFile(path);
        const std::string marker = "MHWVroidHunter-dinput8-proxy";
        return std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) != bytes.end();
    } catch (...) {
        return false;
    }
}

std::wstring gameDirFromUi() {
    wchar_t buf[MAX_PATH * 4];
    GetWindowTextW(g_path, buf, MAX_PATH * 4);
    std::wstring d = buf;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    return d;
}

bool checkGameDir(const std::wstring& dir) {
    if (!g_wnd) return GetFileAttributesW((dir + L"\\" + kGameExe).c_str()) != INVALID_FILE_ATTRIBUTES;
    if (GetFileAttributesW((dir + L"\\" + kGameExe).c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_wnd, L"MonsterHunterWorld.exe was not found in that folder.\nPlease choose the Monster Hunter World game folder.",
                    L"MHW VRoid Hunter", MB_ICONWARNING);
        return false;
    }
    if (gameRunning()) {
        MessageBoxW(g_wnd, L"Please close Monster Hunter World first.", L"MHW VRoid Hunter", MB_ICONWARNING);
        return false;
    }
    return true;
}

void setBusy(bool b) {
    g_busy = b;
    EnableWindow(g_install, !b);
    EnableWindow(g_uninstall, !b);
    EnableWindow(g_choose, !b);
}

bool install(const std::wstring& gameW, std::string& error) {
    std::string game = u(gameW);
    std::string plugins = vh::join(game, "nativePC/plugins");
    try {
        vh::makeDirs(vh::join(plugins, "MHWVroidHunter"));
        vh::writeFile(vh::join(plugins, "MHWVroidHunter.dll"), resourceBytes(IDR_PLUGIN));
        appendLog("Installed nativePC\\plugins\\MHWVroidHunter.dll");
        std::string proxy = vh::join(game, "dinput8.dll");
        if (vh::exists(vh::join(game, "loader.dll"))) {
            appendLog("Stracker's Loader detected - it will load the plugin, dinput8.dll left untouched.");
        } else if (vh::exists(proxy) && !isOurProxy(proxy)) {
            int r = !g_wnd ? IDYES : MessageBoxW(g_wnd,
                                L"There is already a dinput8.dll in the game folder (another mod or loader).\n\n"
                                L"Replace it with the VRoid Hunter loader? A backup named dinput8.dll.vhbackup will be kept.",
                                L"MHW VRoid Hunter", MB_YESNO | MB_ICONQUESTION);
            if (r != IDYES) {
                appendLog("Kept the existing dinput8.dll. If it is not a plugin loader the mod will not start.");
            } else {
                CopyFileW(w(proxy).c_str(), w(proxy + ".vhbackup").c_str(), FALSE);
                vh::writeFile(proxy, resourceBytes(IDR_PROXY));
                appendLog("Installed dinput8.dll loader (old file backed up).");
            }
        } else {
            vh::writeFile(proxy, resourceBytes(IDR_PROXY));
            appendLog("Installed dinput8.dll loader.");
        }
        std::string settings = vh::join(plugins, "MHWVroidHunter/settings.ini");
        if (!vh::exists(settings)) vh::Settings().save(settings);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

void runInstall() {
    std::wstring game = gameDirFromUi();
    if (!checkGameDir(game)) return;
    setBusy(true);
    std::string err;
    if (install(game, err)) {
        appendLog(L"Installed! Next: click \"Choose VRoid model...\" (or press F7 in game).");
    } else {
        appendLog("Install failed: " + err);
        if (err.find("ermission") != std::string::npos || err.find("denied") != std::string::npos)
            appendLog(L"Tip: right-click the installer and choose \"Run as administrator\".");
    }
    setBusy(false);
}

void doUninstall(const std::wstring& gameW) {
        std::string game = u(gameW);
        std::string data = vh::join(game, "nativePC/plugins/MHWVroidHunter");
        try {
            vh::ModManager mm(vh::join(game, "nativePC"), data, [] { return std::unique_ptr<vh::GameSource>(); });
            appendLog(mm.removeAll().message);
            vh::removeFile(vh::join(game, "nativePC/plugins/MHWVroidHunter.dll"));
            std::error_code ec;
            std::filesystem::remove_all(vh::u8(data), ec);
            std::string proxy = vh::join(game, "dinput8.dll");
            if (vh::exists(proxy) && isOurProxy(proxy)) {
                vh::removeFile(proxy);
                if (vh::exists(proxy + ".vhbackup")) std::filesystem::rename(vh::u8(proxy + ".vhbackup"), vh::u8(proxy), ec);
            }
            appendLog(L"Uninstalled. Monster Hunter World is back to normal.");
        } catch (const std::exception& e) {
            appendLog(std::string("Uninstall failed: ") + e.what());
        }
}

void runUninstall() {
    std::wstring gameW = gameDirFromUi();
    if (!checkGameDir(gameW)) return;
    setBusy(true);
    std::thread([gameW] {
        doUninstall(gameW);
        PostMessageW(g_wnd, WM_APP_DONE, 0, 0);
    }).detach();
}

bool doConvert(const std::wstring& gameW, const std::wstring& vrm, bool showArmor);

void runChooseModel() {
    std::wstring gameW = gameDirFromUi();
    if (!checkGameDir(gameW)) return;
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_wnd;
    ofn.lpstrFilter = L"VRoid / VRM model (*.vrm)\0*.vrm\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = L"Choose your VRoid model";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    const bool showArmor = SendMessageW(GetDlgItem(g_wnd, IDC_ARMOR), BM_GETCHECK, 0, 0) == BST_CHECKED;
    setBusy(true);
    std::wstring vrm = file;
    std::thread([gameW, vrm, showArmor] {
        doConvert(gameW, vrm, showArmor);
        PostMessageW(g_wnd, WM_APP_DONE, 0, 0);
    }).detach();
}

bool doConvert(const std::wstring& gameW, const std::wstring& vrm, bool showArmor) {
        std::string game = u(gameW);
        std::string err;
        if (!vh::exists(vh::join(game, "nativePC/plugins/MHWVroidHunter.dll")) && !install(gameW, err)) {
            appendLog("Install failed: " + err);
            return false;
        }
        std::string data = vh::join(game, "nativePC/plugins/MHWVroidHunter");
        std::string settingsPath = vh::join(data, "settings.ini");
        vh::Settings s = vh::Settings::load(settingsPath);
        s.enabled = true;
        s.vrmPath = u(vrm);
        for (bool& b : s.showArmor) b = showArmor;
        try {
            s.save(settingsPath);
            appendLog(L"Converting the model. The first time this reads the game archives and can take a few minutes...");
            vh::ModManager mm(vh::join(game, "nativePC"), data, [game, data] {
                return vh::openArchiveSource(game, vh::join(data, "game_index.cache"), [](const std::string& m) { appendLog(m); });
            });
            vh::ApplyResult r = mm.apply(s, [](const std::string& m) { appendLog(m); });
            if (!r.ok) appendLog("Failed: " + r.message);
            else appendLog(L"All set! Start the game. Press F7 in game to change settings.");
            return r.ok;
        } catch (const std::exception& e) {
            appendLog(std::string("Failed: ") + e.what());
            return false;
        }
}

void browseFolder() {
    BROWSEINFOW bi{};
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = L"Select the Monster Hunter World folder (contains MonsterHunterWorld.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(id, path)) SetWindowTextW(g_path, path);
    CoTaskMemFree(id);
}

HFONT g_font;

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int cx, int cy, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, cx, cy, g_wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return h;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hwnd;
        NONCLIENTMETRICSW ncm{sizeof(ncm)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        make(L"STATIC", L"Play Monster Hunter: World as your VRoid (.vrm) model.", 0, 16, 14, 560, 20, 0);
        make(L"STATIC", L"Monster Hunter World folder:", 0, 16, 44, 560, 20, 0);
        g_path = make(L"EDIT", findGame().c_str(), WS_BORDER | ES_AUTOHSCROLL, 16, 66, 470, 24, IDC_PATH, WS_EX_CLIENTEDGE);
        make(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 494, 65, 90, 26, IDC_BROWSE);
        g_install = make(L"BUTTON", L"1. Install mod", BS_DEFPUSHBUTTON, 16, 104, 180, 34, IDC_INSTALL);
        g_choose = make(L"BUTTON", L"2. Choose VRoid model...", BS_PUSHBUTTON, 204, 104, 200, 34, IDC_CHOOSE);
        g_uninstall = make(L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 412, 104, 172, 34, IDC_UNINSTALL);
        make(L"BUTTON", L"Show hunter armor on top of the model (can be changed per part in game)", BS_AUTOCHECKBOX, 16, 146, 568, 22, IDC_ARMOR);
        g_log = make(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 16, 176, 568, 230, IDC_LOG,
                     WS_EX_CLIENTEDGE);
        if (GetWindowTextLengthW(g_path) == 0)
            appendLog(L"Could not find the game automatically. Click Browse... and pick the Monster Hunter World folder.");
        else
            appendLog(L"Found Monster Hunter World. Click \"1. Install mod\".");
        return 0;
    }
    case WM_COMMAND:
        if (g_busy) return 0;
        switch (LOWORD(wp)) {
        case IDC_BROWSE: browseFolder(); break;
        case IDC_INSTALL: runInstall(); break;
        case IDC_UNINSTALL: runUninstall(); break;
        case IDC_CHOOSE: runChooseModel(); break;
        }
        return 0;
    case WM_APP_LOG: {
        auto* s = reinterpret_cast<std::wstring*>(lp);
        int len = GetWindowTextLengthW(g_log);
        SendMessageW(g_log, EM_SETSEL, len, len);
        SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(s->c_str()));
        delete s;
        return 0;
    }
    case WM_APP_DONE: setBusy(false); return 0;
    case WM_CLOSE:
        if (g_busy && MessageBoxW(hwnd, L"Still working. Quit anyway?", L"MHW VRoid Hunter", MB_YESNO) != IDYES) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// Command line (no window):
//   MHWVroidHunterSetup.exe --install <game folder>
//   MHWVroidHunterSetup.exe --convert <game folder> <model.vrm> [--show-armor]
//   MHWVroidHunterSetup.exe --uninstall <game folder>
// Progress is written to setup_log.txt next to the installer; the exit code is 0 on success.
static int commandLine(int argc, wchar_t** argv) {
    std::wstring mode = argv[1];
    bool ok = false;
    std::wstring game = argc > 2 ? argv[2] : findGame();
    if (mode == L"--install") {
        std::string err;
        ok = checkGameDir(game) && install(game, err);
        if (!err.empty()) appendLog("Install failed: " + err);
    } else if (mode == L"--convert" && argc > 3) {
        bool armor = argc > 4 && std::wstring(argv[4]) == L"--show-armor";
        ok = checkGameDir(game) && doConvert(game, argv[3], armor);
    } else if (mode == L"--uninstall") {
        ok = checkGameDir(game);
        if (ok) doUninstall(game);
    } else {
        appendLog(L"unknown command");
    }
    if (!checkGameDir(game)) appendLog(L"MonsterHunterWorld.exe not found in: " + game);
    wchar_t exe[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
    std::filesystem::path logPath = std::filesystem::path(std::wstring(exe, n)).parent_path() / L"setup_log.txt";
    std::string text = vh::toU8(std::filesystem::path(g_silentLog));
    try {
        vh::writeFile(vh::toU8(logPath), std::vector<uint8_t>(text.begin(), text.end()));
    } catch (...) {
    }
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) return commandLine(argc, argv);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MHWVroidHunterSetup";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    RECT r{0, 0, 600, 420};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MHW VRoid Hunter - Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
