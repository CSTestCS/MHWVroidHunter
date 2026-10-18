// The whole "convert what is in the drop folder and install it" job. Runs in its own process
// (VRoidHunterConverter.exe) so a problem during conversion can never take the game down.
#pragma once

#include <functional>
#include <string>

#include "builder.h"

namespace vh {

enum class JobMode {
    Auto,   // game start: only does work if the model or settings changed since the last run
    Apply,  // F7 "Apply": always re-installs according to the settings
    Remove, // restore the normal hunter
};

struct JobResult {
    bool ok = false;
    bool didWork = false; // false when Auto found nothing to do
    std::string message;
};

std::string pluginDataDir(const std::string& gameDir);

JobResult runJob(const std::string& gameDir, JobMode mode, const std::function<void(const std::string&)>& progress,
                 std::function<std::unique_ptr<GameSource>()> sourceFactory = nullptr);

} // namespace vh
