#include <windows.h>
#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "app.h"
#include "autoconvert.h"
#include "models.h"
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

HANDLE startupDoneEvent() {
    static HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return h;
}

HANDLE startupBegunEvent() {
    static HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return h;
}

bool App::runConverter(const char* mode, bool hidden) {
    if (busy.exchange(true)) return false;
    const std::string exe = vh::join(dataDir, "VRoidHunterConverter.exe");
    if (!vh::exists(exe)) {
        setStatus("VRoidHunterConverter.exe is missing from nativePC\\plugins\\MHWVroidHunter. Please reinstall the mod.");
        busy = false;
        return false;
    }
    std::wstring cmd = L"\"" + vh::u8(exe).wstring() + L"\" " + vh::u8(mode).wstring() + L" \"" + vh::u8(gameDir).wstring() + L"\"";
    if (hidden) cmd += L" --hidden";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        vh::u8(dataDir).wstring().c_str(), &si, &pi)) {
        setStatus("Could not start VRoidHunterConverter.exe (error " + std::to_string(GetLastError()) + ").");
        busy = false;
        return false;
    }
    CloseHandle(pi.hThread);
    const std::string statusFile = vh::join(dataDir, "converter_status.txt");
    std::thread([this, pi, statusFile] {
        std::string lastShown;
        auto poll = [&] {
            std::string last;
            try {
                auto raw = vh::readFile(statusFile);
                std::string text(raw.begin(), raw.end());
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                size_t nl = text.find_last_of('\n');
                last = nl == std::string::npos ? text : text.substr(nl + 1);
            } catch (...) {
            }
            if (last.rfind("#DONE ", 0) == 0) {
                bool ok = last.size() > 6 && last[6] == '1';
                last = (ok ? "" : "Failed: ") + last.substr(std::min<size_t>(last.size(), 8));
            }
            if (!last.empty() && last != lastShown) {
                lastShown = last;
                setStatus(last);
            }
        };
        while (WaitForSingleObject(pi.hProcess, 400) == WAIT_TIMEOUT) poll();
        poll();
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        logLine("converter finished with code " + std::to_string(code));
        busy = false;
        refreshModels();
    }).detach();
    return true;
}

void App::startApply() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        dirty = false;
        saveSettings();
    }
    if (runConverter("--apply", true)) setStatus("Converting in the background - you can keep playing.");
}

void App::startRemove() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        settings.enabled = false;
        saveSettings();
    }
    runConverter("--remove", true);
}

void App::openModelsFolder() {
    try {
        vh::ensureModelsFolder(gameDir);
    } catch (...) {
    }
    // Start Explorer as its own process (no shell dialogs inside the game process).
    std::wstring cmd = L"explorer.exe \"" + vh::u8(vh::modelsFolder(gameDir)).wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void App::refreshModels() {
    std::vector<std::string> names;
    for (auto& m : vh::listModels(gameDir)) names.push_back(m.name);
    std::lock_guard<std::mutex> lk(mutex);
    models = names;
    std::string active = vh::resolveModel(gameDir, settings);
    activeModel = active.empty() ? "" : vh::toU8(vh::u8(active).filename());
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
    ImGui::TextWrapped("Put your .vrm file in the \"VRoidHunter\" folder inside the game folder.");
    if (ImGui::Button("Open models folder")) a.openModelsFolder();
    ImGui::SameLine();
    if (ImGui::Button("Refresh list")) {
        a.mutex.unlock();
        a.refreshModels();
        a.mutex.lock();
    }
    if (a.models.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "No .vrm file found in the folder yet.");
    } else {
        const std::string preview = a.activeModel.empty() ? std::string("(choose)") : a.activeModel;
        if (ImGui::BeginCombo("Model", preview.c_str())) {
            for (size_t i = 0; i < a.models.size(); ++i) {
                const std::string& name = a.models[i];
                std::string label = i == 0 ? name + "  (newest)" : name;
                if (ImGui::Selectable(label.c_str(), name == a.activeModel)) {
                    vh::pickModel(a.gameDir, s, name);
                    a.activeModel = name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }
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
    if (t > 25.0f) return;
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##vhhint", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    int hk;
    std::string status;
    {
        std::lock_guard<std::mutex> lk(app().mutex);
        hk = app().settings.hotkey;
        status = app().status;
    }
    ImGui::Text("VRoid Hunter loaded - press F%d for settings", hk >= VK_F1 && hk <= VK_F12 ? hk - VK_F1 + 1 : 7);
    // result of the start-up conversion, so problems are visible without opening the menu
    const bool failed = status.rfind("Failed", 0) == 0 || status.find("Could not") != std::string::npos;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    if (failed) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", status.c_str());
    else ImGui::TextUnformatted(status.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
}

} // namespace vhp
