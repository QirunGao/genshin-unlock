#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>

namespace z3lx::runtime {

using namespace z3lx::shared;

struct RuntimeInitParams {
    ConfigSnapshotMessage config;
    uint32_t gamePid = 0;
    void* ipcPipeHandle = nullptr;   // HANDLE — kept alive by bootstrap
};

struct RuntimeInitResult {
    StatusCode status = StatusCode::Ok;
    bool fpsAvailable = false;
    bool fovAvailable = false;
};

extern "C" __declspec(dllexport)
RuntimeInitResult RuntimeInitialize(const RuntimeInitParams* params);

} // namespace z3lx::runtime
