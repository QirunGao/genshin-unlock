#pragma once

#include "shared/ConfigModel.hpp"

#include <cstdint>

#include <Windows.h>

namespace z3lx::launcher {

using namespace z3lx::shared;

struct ProcessContext {
    HANDLE processHandle = nullptr;
    HANDLE threadHandle = nullptr;
    uint32_t processId = 0;
    uint32_t threadId = 0;
};

ProcessContext StartGameProcess(const LauncherConfig& config);

} // namespace z3lx::launcher
