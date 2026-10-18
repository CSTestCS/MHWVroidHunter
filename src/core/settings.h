// User settings, stored as a small key=value file next to the plugin.
#pragma once

#include <string>

#include "hunter.h"

namespace vh {

struct Settings {
    bool enabled = false;               // play as the VRoid model
    std::string vrmPath;                // UTF-8 path of the .vrm file
    bool showArmor[kSlotCount] = {false, false, false, false, false}; // per slot: show hunter armour instead
    float conform = 1.0f;               // 1 = fit VRoid body to the hunter skeleton (best armour fit)
    bool dropTransparent = true;
    int maxTextureSize = 2048;
    int hotkey = 0x76;                  // VK_F7
    bool applyToMale = true;
    bool applyToFemale = true;

    static Settings load(const std::string& path);
    void save(const std::string& path) const;
    // Settings that change the generated model (used to decide whether a rebuild is needed).
    std::string modelKey() const;
};

} // namespace vh
