#pragma once

#include "shared/StatusCode.hpp"

#include <filesystem>

#include <Windows.h>

namespace z3lx::bootstrap {

using namespace z3lx::shared;

struct RuntimeLoadResult {
    StatusCode status = StatusCode::Ok;
    HMODULE runtimeModule = nullptr;
};

RuntimeLoadResult LoadRuntime(const std::filesystem::path& runtimePath);

void UnloadRuntime(HMODULE runtimeModule);

} // namespace z3lx::bootstrap
