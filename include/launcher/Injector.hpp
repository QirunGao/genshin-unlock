#pragma once

#include "shared/StatusCode.hpp"

#include <filesystem>

#include <Windows.h>

namespace z3lx::launcher {

using namespace z3lx::shared;

StatusCode InjectBootstrap(
    HANDLE processHandle,
    const std::filesystem::path& bootstrapPath);

} // namespace z3lx::launcher
