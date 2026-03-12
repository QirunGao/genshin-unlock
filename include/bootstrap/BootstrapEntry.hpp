#pragma once

#include "shared/StatusCode.hpp"

#include <Windows.h>

namespace z3lx::bootstrap {

using namespace z3lx::shared;

struct BootstrapInitParams {
    HMODULE bootstrapModule = nullptr;
};

struct BootstrapInitResult {
    StatusCode status = StatusCode::Ok;
    bool runtimeLoaded = false;
    bool runtimeInitialized = false;
};

extern "C" __declspec(dllexport)
BootstrapInitResult BootstrapInitialize(const BootstrapInitParams* params);

} // namespace z3lx::bootstrap
