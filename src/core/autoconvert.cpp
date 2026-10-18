#include "autoconvert.h"

#include "fsutil.h"
#include "models.h"

namespace vh {

std::string pluginDataDir(const std::string& gameDir) { return join(gameDir, "nativePC/plugins/MHWVroidHunter"); }
std::string gameDataCacheDir(const std::string& gameDir) { return join(pluginDataDir(gameDir), "game_data"); }

namespace {

std::string stateKey(const Settings& s) {
    if (!s.enabled) return "disabled";
    std::string k = s.modelKey() + "|armor:";
    for (bool b : s.showArmor) k += b ? '1' : '0';
    k += s.applyToFemale ? "|f" : "|-";
    k += s.applyToMale ? "m" : "-";
    return k;
}

std::string readText(const std::string& path) {
    try {
        auto b = readFile(path);
        return std::string(b.begin(), b.end());
    } catch (...) {
        return "";
    }
}

} // namespace

bool modelUpToDate(const std::string& gameDir) {
    try {
        const std::string data = pluginDataDir(gameDir);
        Settings s = Settings::load(join(data, "settings.ini"));
        s.vrmPath = resolveModel(gameDir, s);
        if (s.enabled && s.vrmPath.empty()) return true; // nothing to install
        return readText(join(data, "applied_state.txt")) == stateKey(s);
    } catch (...) {
        return false;
    }
}

JobResult runJob(const std::string& gameDir, JobMode mode, const std::function<void(const std::string&)>& progress,
                 std::function<std::unique_ptr<GameSource>()> sourceFactory) {
    JobResult r;
    const std::string data = pluginDataDir(gameDir);
    const std::string keyFile = join(data, "applied_state.txt");
    try {
        makeDirs(data);
        ensureModelsFolder(gameDir);
        if (!sourceFactory)
            sourceFactory = [gameDir, progress] { return openCachedGameData(gameDir, gameDataCacheDir(gameDir), progress); };
        ModManager mm(join(gameDir, "nativePC"), data, sourceFactory);

        if (mode == JobMode::Remove) {
            ApplyResult a = mm.removeAll();
            removeFile(keyFile);
            r.ok = a.ok;
            r.didWork = true;
            r.message = a.message;
            return r;
        }

        Settings s = Settings::load(join(data, "settings.ini"));
        s.vrmPath = resolveModel(gameDir, s);
        if (s.enabled && s.vrmPath.empty()) {
            r.ok = true;
            r.message = "No .vrm file yet. Put one in the \"VRoidHunter\" folder in the game folder.";
            return r;
        }
        const std::string key = stateKey(s);
        if (mode == JobMode::Auto && readText(keyFile) == key) {
            r.ok = true;
            r.message = "Model is up to date.";
            return r;
        }
        if (!s.enabled && readText(keyFile).empty() && !exists(mm.manifestPath())) {
            r.ok = true;
            r.message = "Mod is turned off.";
            return r;
        }
        r.didWork = true;
        ApplyResult a = mm.apply(s, progress);
        r.ok = a.ok;
        r.message = a.message;
        if (a.ok) {
            writeFile(keyFile, std::vector<uint8_t>(key.begin(), key.end()));
        } else {
            removeFile(keyFile);
        }
    } catch (const std::exception& e) {
        r.ok = false;
        r.message = e.what();
    }
    return r;
}

} // namespace vh
