// The drop folder: <game>/VRoidHunter. Any .vrm placed there is picked up automatically.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "settings.h"

namespace vh {

std::string modelsFolder(const std::string& gameDir);
// Creates the folder (with a short read-me) if it does not exist yet.
void ensureModelsFolder(const std::string& gameDir);

struct ModelEntry {
    std::string name; // file name, e.g. "MyModel.vrm"
    std::string path; // full UTF-8 path
    int64_t mtime = 0;
    uint64_t size = 0;
};
// All .vrm files in the drop folder, newest first.
std::vector<ModelEntry> listModels(const std::string& gameDir);

// Which model file to use:
//  * the model picked in the menu (settings.modelName), unless a newer file was dropped in after
//    that choice was made,
//  * otherwise the newest .vrm in the drop folder,
//  * otherwise settings.vrmPath (an explicit path set by the command line tools).
// Returns "" when there is no model.
std::string resolveModel(const std::string& gameDir, const Settings& s);

// Remembers `name` as the chosen model (used by the menu).
void pickModel(const std::string& gameDir, Settings& s, const std::string& name);

} // namespace vh
