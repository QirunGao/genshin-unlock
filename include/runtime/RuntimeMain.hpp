#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>

namespace z3lx::runtime {

using namespace z3lx::shared;

struct RuntimeInitParams {
    ConfigSnapshotMessage config;
    uint32_t gamePid = 0;
    char gameVersion[kMaxStringLen] = {};
    void* ipcPipeHandle = nullptr;   // HANDLE — kept alive by bootstrap
};

struct RuntimeInitResult {
    StatusCode status = StatusCode::Ok;
    bool fpsAvailable = false;
    bool fovAvailable = false;
    uint32_t runtimeState = 0;
    uint32_t systemError = 0;
    char phaseName[kMaxStringLen] = {};
    char message[256] = {};
};

struct ControlPlaneStartResult {
    StatusCode status = StatusCode::Ok;
    uint32_t runtimeState = 0;
    uint32_t systemError = 0;
    char phaseName[kMaxStringLen] = {};
    char message[256] = {};
};

extern "C" __declspec(dllexport)
RuntimeInitResult RuntimeInitialize(const RuntimeInitParams* params);

extern "C" __declspec(dllexport)
ControlPlaneStartResult RuntimeStartControlPlane();

} // namespace z3lx::runtime
