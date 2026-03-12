#pragma once

#include "util/win/VirtualKey.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace z3lx::shared {

enum class DisplayMode : uint8_t {
    Windowed = 0,
    Fullscreen = 1,
    Borderless = 2
};

struct LauncherConfig {
    std::filesystem::path gamePath {};
    bool overrideArgs = false;
    uint8_t monitorIndex = 1;
    DisplayMode displayMode = DisplayMode::Fullscreen;
    uint16_t screenWidth = 1920;
    uint16_t screenHeight = 1080;
    bool mobilePlatform = false;
    std::string additionalArgs {};
    bool closeLauncherOnSuccess = false;
    std::string logLevel = "info";

    void Serialize(std::vector<uint8_t>& buffer);
    void Deserialize(const std::vector<uint8_t>& buffer);
};

struct RuntimeConfig {
    bool unlockFps = true;
    int targetFps = 120;
    bool autoThrottle = true;

    bool unlockFov = true;
    int targetFov = 90;
    std::vector<int> fovPresets { 30, 45, 60, 75, 90, 110 };
    float fovSmoothing = 0.125f;
    util::VirtualKey unlockFovKey = util::VirtualKey::DownArrow;
    util::VirtualKey nextFovPresetKey = util::VirtualKey::RightArrow;
    util::VirtualKey prevFovPresetKey = util::VirtualKey::LeftArrow;

    void Serialize(std::vector<uint8_t>& buffer);
    void Deserialize(const std::vector<uint8_t>& buffer);
};

} // namespace z3lx::shared
