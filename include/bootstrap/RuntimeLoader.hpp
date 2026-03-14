#pragma once

#include "shared/StatusCode.hpp"

#include <filesystem>
#include <string>

#include <Windows.h>

namespace z3lx::bootstrap {

using namespace z3lx::shared;

struct RuntimeLoadResult {
    StatusCode status = StatusCode::Ok;
    uint32_t systemError = 0;
    std::string phaseName = "runtime_load";
    std::string message;
    HMODULE runtimeModule = nullptr;
};

RuntimeLoadResult LoadRuntime(
    const std::filesystem::path& runtimePath,
    const std::string& expectedSha256Hex);

void UnloadRuntime(HMODULE runtimeModule);

} // namespace z3lx::bootstrap
