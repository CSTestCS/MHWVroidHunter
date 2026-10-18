// Turns a VRM file + settings into loose files under <game>/nativePC that the game loads instead
// of the hunter's armour, face and hair models.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "chunk.h"
#include "hunter.h"
#include "retarget.h"
#include "settings.h"

namespace vh {

// Read-only access to the game's original files.
class GameSource {
public:
    virtual ~GameSource() = default;
    // Relevant player model/material paths (normalised, see normalizeGamePath).
    virtual std::vector<std::string> list() = 0;
    virtual std::vector<uint8_t> read(const std::string& path) = 0;
};

// Reads directly from chunkG*.bin. The file list is cached in `cacheFile`.
std::unique_ptr<GameSource> openArchiveSource(const std::string& gameDir, const std::string& cacheFile,
                                              const std::function<void(const std::string&)>& progress);
// Reads from a folder of already extracted files (e.g. a WorldChunkTool dump); used by tests/CLI.
std::unique_ptr<GameSource> openFolderSource(const std::string& folder);

// Paths the mod cares about.
bool isRelevantGamePath(const std::string& normalizedPath);

struct ApplyResult {
    bool ok = false;
    std::string message;          // short summary for the UI
    std::vector<std::string> log; // detailed lines
    int linkedFiles = 0;
    int skippedConflicts = 0;     // paths owned by other mods that were left untouched
};

class ModManager {
public:
    // nativePC: <game>/nativePC; dataDir: folder for the cache, manifest and report.
    ModManager(std::string nativePC, std::string dataDir, std::function<std::unique_ptr<GameSource>()> sourceFactory);

    // Builds (if needed) and installs the model according to `s`. When s.enabled is false all
    // files created by the mod are removed. Thread-safe to call from a worker thread.
    ApplyResult apply(const Settings& s, const std::function<void(const std::string&)>& progress = nullptr);
    // Removes everything this mod created in nativePC.
    ApplyResult removeAll();

    std::string manifestPath() const;

private:
    struct GenderData {
        char g;                     // 'f' or 'm'
        std::string templateSet;    // e.g. pl001_0000
        std::map<Slot, std::string> slotMod3, slotMrl3;
    };

    void loadManifest();
    void saveManifest() const;
    bool buildModel(const Settings& s, GameSource& src, const std::vector<std::string>& files, const std::string& modelDir,
                    std::vector<std::string>& log, const std::function<void(const std::string&)>& progress);

    std::string nativePC_;
    std::string dataDir_;
    std::function<std::unique_ptr<GameSource>()> sourceFactory_;
    std::set<std::string> manifest_; // files we created, relative to nativePC (forward slashes)
};

} // namespace vh
