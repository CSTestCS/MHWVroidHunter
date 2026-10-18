// Shared state of the in-game plugin.
#pragma once

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "settings.h"

namespace vhp {

struct App {
    std::string gameDir;      // folder of MonsterHunterWorld.exe (UTF-8)
    std::string nativePC;     // <game>/nativePC
    std::string dataDir;      // <game>/nativePC/plugins/MHWVroidHunter
    std::string settingsPath; // <dataDir>/settings.ini

    std::mutex mutex;         // guards everything below
    vh::Settings settings;
    std::string status = "Ready.";
    std::vector<std::string> log;
    bool dirty = false;       // settings changed since the last apply

    std::vector<std::string> models; // .vrm files in the drop folder, newest first (refreshed in the background)
    std::string activeModel;         // file the converter will use

    std::atomic<bool> busy{false};   // the converter process is running
    std::atomic<bool> menuVisible{false};
    std::atomic<bool> overlayActive{false}; // the in-game (DX11) overlay is drawing frames

    void saveSettings();             // caller holds `mutex`
    // Run VRoidHunterConverter.exe ("--auto", "--apply" or "--remove") in its own process and
    // follow its progress. The game itself never converts anything.
    bool runConverter(const char* mode, bool hidden);
    void startApply();
    void startRemove();
    void openModelsFolder();
    void refreshModels();
    void setStatus(const std::string& s);
};

// Signalled once the start-up conversion (if any) has finished.
HANDLE startupDoneEvent();
// Signalled as soon as the plugin's start-up thread is running.
HANDLE startupBegunEvent();

App& app();
void logLine(const std::string& line);

// ImGui contents of the settings menu. The caller owns the ImGui frame.
void drawSettingsUI(bool standaloneWindow);
// Loads a font with Japanese glyphs (Meiryo / Yu Gothic / MS Gothic) when the system has one.
void setupFonts(float size);
// Text shown in the corner while the menu is closed for a few seconds after start-up.
void drawStartupHint();

// DirectX 11 overlay (hooks IDXGISwapChain::Present).
bool installOverlay();
// Separate always-on-top settings window (used when the game runs in DirectX 12 mode).
void toggleStandaloneWindow();
// Serialises ImGui use between the overlay and the standalone window.
// Recursive: Win32 calls made by ImGui (e.g. ReleaseCapture) re-enter the window procedure
// on the same thread while the lock is held.
std::recursive_mutex& imguiMutex();

} // namespace vhp
