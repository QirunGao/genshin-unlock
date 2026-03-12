#pragma once

#include "shared/ConfigModel.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace z3lx::launcher {

LauncherConfig ReadLauncherConfig(const std::filesystem::path& configFilePath);
RuntimeConfig ReadRuntimeConfig(const std::filesystem::path& configFilePath);

void WriteLauncherConfig(
    const std::filesystem::path& configFilePath,
    const LauncherConfig& config);

void WriteRuntimeConfig(
    const std::filesystem::path& configFilePath,
    const RuntimeConfig& config);

using namespace z3lx::shared;

} // namespace z3lx::launcher
