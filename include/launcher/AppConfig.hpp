#pragma once

#include "shared/ConfigModel.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace z3lx::launcher {

using z3lx::shared::LauncherConfig;
using z3lx::shared::RuntimeConfig;

LauncherConfig ReadLauncherConfig(const std::filesystem::path& configFilePath);
RuntimeConfig ReadRuntimeConfig(const std::filesystem::path& configFilePath);

void WriteLauncherConfig(
    const std::filesystem::path& configFilePath,
    const LauncherConfig& config);

void WriteRuntimeConfig(
    const std::filesystem::path& configFilePath,
    const RuntimeConfig& config);

} // namespace z3lx::launcher
