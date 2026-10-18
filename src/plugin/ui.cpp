#include <windows.h>
#include <commdlg.h>
#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "app.h"
#include "builder.h"
#include "fsutil.h"

namespace vhp {

App& app() {
    static App a;
    return a;
}

void logLine(const std::string& line) {
    App& a = app();
    try {
        std::string path = vh::join(a.dataDir, "log.txt");
        std::FILE* f = _wfopen(vh::u8(path).wstring().c_str(), L"ab");
        if (f) {
            std::fprintf(f, "%s\n", line.c_str());
            std::fclose(f);
        }
    } catch (...) {
    }
}

void App::setStatus(const std::string& s) {
    {
        std::lock_guard<std::mutex> lk(mutex);
        status = s;
        log.push_back(s);
        if (log.size() > 200) log.erase(log.begin());
    }
    logLine(s);
}

void App::saveSettings() {
    try {
        settings.save(settingsPath);
    } catch (const std::exception& e) {
        status = std::string("Could not save settings: ") + e.what();
    }
}

static vh::ModManager makeManager(App& a) {
    std::string game = a.gameDir, data = a.dataDir;
    return vh::ModManager(a.nativePC, a.dataDir, [game, data] {
        return vh::openArchiveSource(game, vh::join(data, "game_index.cache"), [](const std::string& m) { app().setStatus(m); });
    });
}

void App::startApply() {
    if (busy.exchange(true)) return;
    vh::Settings snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex);
        snapshot = settings;
        dirty = false;
        saveSettings();
    }
    std::thread([this, snapshot] {
        setStatus("Working... the game may stutter for a moment.");
        try {
            vh::ModManager mm = makeManager(*this);
            vh::ApplyResult r = mm.apply(snapshot, [this](const std::string& m) { setStatus(m); });
            setStatus(r.ok ? r.message : "Failed: " + r.message);
        } catch (const std::exception& e) {
            setStatus(std::string("Failed: ") + e.what());
        }
        busy = false;
    }).detach();
}

void App::startRemove() {
    if (busy.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lk(mutex);
        settings.enabled = false;
        saveSettings();
    }
    std::thread([this] {
        try {
            vh::ModManager mm = makeManager(*this);
            setStatus(mm.removeAll().message);
        } catch (const std::exception& e) {
            setStatus(std::string("Failed: ") + e.what());
        }
        busy = false;
    }).detach();
}

void App::chooseVrmFile() {
    // The file dialog blocks, so it runs on its own thread.
    std::thread([this] {
        wchar_t file[MAX_PATH * 4] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = L"VRoid / VRM model (*.vrm)\0*.vrm\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH * 4;
        ofn.lpstrTitle = L"Choose your VRoid model";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn)) {
            std::string path = vh::toU8(std::filesystem::path(file));
            {
                std::lock_guard<std::mutex> lk(mutex);
                settings.vrmPath = path;
                settings.enabled = true;
                dirty = true;
                saveSettings();
            }
            setStatus("Model selected. Press \"Apply\" to convert it.");
        }
    }).detach();
}

void setupFonts(float size) {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    for (const wchar_t* name : {L"meiryo.ttc", L"YuGothM.ttc", L"msgothic.ttc"}) {
        std::filesystem::path p = std::filesystem::path(winDir) / L"Fonts" / name;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 1;
        if (io.Fonts->AddFontFromFileTTF(vh::toU8(p).c_str(), size, &cfg, io.Fonts->GetGlyphRangesJapanese())) return;
    }
    ImFontConfig cfg;
    cfg.SizePixels = size;
    io.Fonts->AddFontDefault(&cfg);
}

static void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void drawSettingsUI(bool standalone) {
    App& a = app();
    std::lock_guard<std::mutex> lk(a.mutex);
    vh::Settings& s = a.settings;
    const bool busy = a.busy;
    bool changed = false;

    if (!standalone) {
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
        bool open = true;
        if (!ImGui::Begin("VRoid Hunter settings", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }
        if (!open) a.menuVisible = false;
    }

    ImGui::BeginDisabled(busy);
    changed |= ImGui::Checkbox("Play as my VRoid model", &s.enabled);

    ImGui::SeparatorText("Model");
    std::string shown = s.vrmPath.empty() ? std::string("(no model chosen)") : std::filesystem::path(vh::u8(s.vrmPath)).filename().u8string();
    ImGui::TextUnformatted(shown.c_str());
    if (!s.vrmPath.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.vrmPath.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Choose .vrm file...")) a.chooseVrmFile();
    ImGui::TextDisabled("The model is resized to your hunter's height automatically.");

    ImGui::SeparatorText("Hunter armor");
    ImGui::TextUnformatted("Show armor on top of the VRoid body:");
    for (int i = 0; i < vh::kSlotCount; ++i) {
        if (i) ImGui::SameLine();
        changed |= ImGui::Checkbox(vh::slotLabel(vh::Slot(i)), &s.showArmor[i]);
    }
    if (ImGui::SmallButton("Show all armor")) {
        for (bool& b : s.showArmor) b = true;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide all armor")) {
        for (bool& b : s.showArmor) b = false;
        changed = true;
    }
    helpMarker("Checked parts use your equipped (or layered) armor, fitted to the VRoid body. "
               "Unchecked parts show the VRoid model. Showing the head armor also brings back "
               "your hunter's face and hair under open helmets.");

    float fit = s.conform * 100.0f;
    if (ImGui::SliderFloat("Body fit", &fit, 0.0f, 100.0f, "%.0f%%")) {
        s.conform = fit / 100.0f;
        changed = true;
    }
    helpMarker("100% moves the VRoid joints onto the hunter's joints so armor and animations line up "
               "perfectly. Lower values keep more of the VRoid's own proportions but armor and elbows/knees "
               "may not line up.");

    if (ImGui::CollapsingHeader("Advanced")) {
        changed |= ImGui::Checkbox("Female hunter", &s.applyToFemale);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Male hunter", &s.applyToMale);
        changed |= ImGui::Checkbox("Remove fully transparent parts", &s.dropTransparent);
        const int sizes[] = {1024, 2048, 4096};
        int sel = s.maxTextureSize <= 1024 ? 0 : (s.maxTextureSize <= 2048 ? 1 : 2);
        if (ImGui::Combo("Max texture size", &sel, "1024\0" "2048\0" "4096\0")) {
            s.maxTextureSize = sizes[sel];
            changed = true;
        }
    }
    ImGui::EndDisabled();
    if (changed) {
        a.dirty = true;
        a.saveSettings();
    }

    ImGui::Separator();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(a.dirty ? "Apply changes *" : "Apply", ImVec2(160, 0))) {
        a.mutex.unlock();
        a.startApply();
        a.mutex.lock();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove mod files", ImVec2(160, 0))) {
        a.mutex.unlock();
        a.startRemove();
        a.mutex.lock();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s%s", busy ? "[working] " : "", a.status.c_str());
    ImGui::TextDisabled("After applying, change equipment, enter your room or travel so the game reloads your hunter.");
    char hk[64];
    std::snprintf(hk, sizeof(hk), "Open/close this menu with F%d.", s.hotkey >= VK_F1 && s.hotkey <= VK_F12 ? s.hotkey - VK_F1 + 1 : 7);
    ImGui::TextDisabled("%s", hk);

    if (!standalone) ImGui::End();
}

void drawStartupHint() {
    static const auto start = std::chrono::steady_clock::now();
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    if (t > 12.0f) return;
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##vhhint", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    int hk;
    {
        std::lock_guard<std::mutex> lk(app().mutex);
        hk = app().settings.hotkey;
    }
    ImGui::Text("VRoid Hunter loaded - press F%d for settings", hk >= VK_F1 && hk <= VK_F12 ? hk - VK_F1 + 1 : 7);
    ImGui::End();
}

} // namespace vhp
