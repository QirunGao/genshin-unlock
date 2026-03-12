#pragma once

#include "launcher/AppConfig.hpp"

#include <wil/resource.h>

#include <Windows.h>

namespace z3lx::launcher {

struct GameProcess {
    wil::unique_handle process;
    wil::unique_handle mainThread;
    DWORD              processId = 0;
};

// Creates the game process in a CREATE_SUSPENDED state.
// The caller is responsible for resuming the main thread after injection.
[[nodiscard]] GameProcess StartGameProcess(const LauncherConfig& config);

} // namespace z3lx::launcher
