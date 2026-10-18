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

    std::atomic<bool> busy{false};
    std::atomic<bool> menuVisible{false};
    std::atomic<bool> overlayActive{false}; // the in-game (DX11) overlay is drawing frames

    void saveSettings();
    void startApply();
    void startRemove();
    void chooseVrmFile();
    void setStatus(const std::string& s);
};

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
std::mutex& imguiMutex();

} // namespace vhp
