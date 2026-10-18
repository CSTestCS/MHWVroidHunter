#include "settings.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>

#include "fsutil.h"

namespace vh {

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool toBool(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on"; }
} // namespace

Settings Settings::load(const std::string& path) {
    Settings s;
    if (!exists(path)) return s;
    std::vector<uint8_t> raw;
    try {
        raw = readFile(path);
    } catch (...) {
        return s;
    }
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    std::map<std::string, std::string> kv;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    auto get = [&](const char* k) -> const std::string* {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    };
    if (auto v = get("enabled")) s.enabled = toBool(*v);
    if (auto v = get("vrm_path")) s.vrmPath = *v;
    if (auto v = get("model")) s.modelName = *v;
    if (auto v = get("model_picked_time")) s.modelPickedNewest = std::strtoll(v->c_str(), nullptr, 10);
    for (int i = 0; i < kSlotCount; ++i) {
        std::string key = std::string("show_armor_") + slotFolder(Slot(i));
        if (auto v = get(key.c_str())) s.showArmor[i] = toBool(*v);
    }
    if (auto v = get("conform")) s.conform = clamp01(std::strtof(v->c_str(), nullptr));
    if (auto v = get("drop_transparent")) s.dropTransparent = toBool(*v);
    if (auto v = get("max_texture_size")) s.maxTextureSize = std::max(256, std::min(4096, std::atoi(v->c_str())));
    if (auto v = get("hotkey")) s.hotkey = std::atoi(v->c_str());
    if (auto v = get("apply_male")) s.applyToMale = toBool(*v);
    if (auto v = get("apply_female")) s.applyToFemale = toBool(*v);
    return s;
}

void Settings::save(const std::string& path) const {
    std::ostringstream o;
    o << "# MHW VRoid Hunter settings (edit in game with the hotkey, default F7).\n";
    o << "# Put your .vrm file in the \"VRoidHunter\" folder next to MonsterHunterWorld.exe.\n";
    o << "enabled=" << (enabled ? 1 : 0) << "\n";
    o << "vrm_path=" << vrmPath << "\n";
    o << "model=" << modelName << "\n";
    o << "model_picked_time=" << modelPickedNewest << "\n";
    for (int i = 0; i < kSlotCount; ++i) o << "show_armor_" << slotFolder(Slot(i)) << "=" << (showArmor[i] ? 1 : 0) << "\n";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", conform);
    o << "conform=" << buf << "\n";
    o << "drop_transparent=" << (dropTransparent ? 1 : 0) << "\n";
    o << "max_texture_size=" << maxTextureSize << "\n";
    o << "hotkey=" << hotkey << "\n";
    o << "apply_male=" << (applyToMale ? 1 : 0) << "\n";
    o << "apply_female=" << (applyToFemale ? 1 : 0) << "\n";
    std::string s = o.str();
    writeFile(path, std::vector<uint8_t>(s.begin(), s.end()));
}

std::string Settings::modelKey() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "|%.3f|%d|%d", conform, dropTransparent ? 1 : 0, maxTextureSize);
    // file name, size and time only: the same file can be reached through differently written
    // paths (installer vs game), which must not count as a different model
    return toU8(u8(vrmPath).filename()) + buf + "|" + std::to_string(fileSize(vrmPath)) + "|" + std::to_string(fileTime(vrmPath));
}

} // namespace vh
