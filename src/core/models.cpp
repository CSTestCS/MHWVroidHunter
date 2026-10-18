#include "models.h"

#include <algorithm>
#include <cctype>

#include "fsutil.h"

namespace fs = std::filesystem;

namespace vh {

std::string modelsFolder(const std::string& gameDir) { return join(gameDir, "VRoidHunter"); }

void ensureModelsFolder(const std::string& gameDir) {
    std::string dir = modelsFolder(gameDir);
    makeDirs(dir);
    std::string readme = join(dir, "PUT YOUR .VRM FILE HERE.txt");
    if (!exists(readme)) {
        std::string text =
            "Copy your VRoid model (.vrm) into this folder, then start Monster Hunter: World.\r\n"
            "The model is converted automatically when the game starts (the first time can take a few minutes).\r\n"
            "\r\n"
            "If there are several .vrm files, the newest one is used. You can switch between them in the\r\n"
            "in-game menu (F7).\r\n";
        writeFile(readme, std::vector<uint8_t>(text.begin(), text.end()));
    }
}

std::vector<ModelEntry> listModels(const std::string& gameDir) {
    std::vector<ModelEntry> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(u8(modelsFolder(gameDir)), ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = toU8(e.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".vrm") continue;
        ModelEntry m;
        m.name = toU8(e.path().filename());
        m.path = toU8(e.path());
        m.mtime = fileTime(m.path);
        m.size = fileSize(m.path);
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const ModelEntry& a, const ModelEntry& b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.name < b.name;
    });
    return out;
}

std::string resolveModel(const std::string& gameDir, const Settings& s) {
    std::vector<ModelEntry> models = listModels(gameDir);
    if (!models.empty()) {
        if (!s.modelName.empty() && models.front().mtime <= s.modelPickedNewest)
            for (auto& m : models)
                if (m.name == s.modelName) return m.path;
        return models.front().path;
    }
    if (!s.vrmPath.empty() && exists(s.vrmPath)) return s.vrmPath;
    return "";
}

void pickModel(const std::string& gameDir, Settings& s, const std::string& name) {
    std::vector<ModelEntry> models = listModels(gameDir);
    s.modelName = name;
    s.modelPickedNewest = models.empty() ? 0 : models.front().mtime;
}

} // namespace vh
