#pragma once

#include "shared/ConfigModel.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace z3lx::launcher {

enum class DisplayMode : uint8_t {
    Windowed   = 0,
    Fullscreen = 1,
    Borderless = 2,
};

enum class LogLevel : uint8_t {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3,
};

// Configuration for the launcher itself (launcher_config.json).
// Does NOT contain dllPaths – only bootstrap.dll and runtime.dll (fixed paths)
// are ever loaded into the game process.
struct LauncherConfig {
    std::filesystem::path gamePath {};
    bool                  overrideArgs          = false;
    uint8_t               monitorIndex          = 1;
    DisplayMode           displayMode           = DisplayMode::Fullscreen;
    uint16_t              screenWidth           = 1920;
    uint16_t              screenHeight          = 1080;
    bool                  mobilePlatform        = false;
    std::string           additionalArgs        {};
    bool                  closeLauncherOnSuccess = false;
    LogLevel              logLevel              = LogLevel::Info;

    void Serialize(std::vector<uint8_t>& buffer);
    void Deserialize(const std::vector<uint8_t>& buffer);
};

// Runtime config read from runtime_config.json by the launcher and forwarded
// to the runtime via IPC. The runtime never reads files directly.
struct RuntimeConfigFile {
    shared::RuntimeConfig config {};

    void Serialize(std::vector<uint8_t>& buffer);
    void Deserialize(const std::vector<uint8_t>& buffer);
};

} // namespace z3lx::launcher
