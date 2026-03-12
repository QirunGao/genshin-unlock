#pragma once

#include "util/win/VirtualKey.hpp"

#include <cstdint>
#include <vector>

namespace z3lx::shared {

// Runtime config snapshot. Launcher reads runtime_config.json and sends
// this to the runtime via IPC. The runtime never reads config files.
struct RuntimeConfig {
    bool unlockFps = true;
    int targetFps = 120;
    bool autoThrottle = true;

    bool unlockFov = true;
    int targetFov = 90;
    std::vector<int> fovPresets { 30, 45, 60, 75, 90, 110 };
    float fovSmoothing = 0.125f;

    util::VirtualKey unlockFovKey    = util::VirtualKey::DownArrow;
    util::VirtualKey nextFovPresetKey = util::VirtualKey::RightArrow;
    util::VirtualKey prevFovPresetKey = util::VirtualKey::LeftArrow;
};

} // namespace z3lx::shared
