#include "bootstrap/BootstrapEntry.hpp"
#include "bootstrap/HostValidator.hpp"
#include "bootstrap/IpcServer.hpp"
#include "bootstrap/RuntimeLoader.hpp"
#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <filesystem>

#include <Windows.h>

namespace z3lx::bootstrap {

extern HMODULE GetBootstrapModule() noexcept;

extern "C" __declspec(dllexport)
BootstrapInitResult BootstrapInitialize(const BootstrapInitParams* params) {
    BootstrapInitResult result {};

    // Step 1: Validate host process
    const auto hostResult = ValidateHost();
    if (hostResult.status != StatusCode::Ok) {
        result.status = hostResult.status;
        return result;
    }

    // Step 2: Establish IPC with launcher
    const uint32_t gamePid = GetCurrentProcessId();
    IpcServer ipc { gamePid };
    if (ipc.Create() != StatusCode::Ok) {
        // IPC is optional for backward compatibility
    }

    // Step 3: Load runtime.dll from the bootstrap directory
    HMODULE bsModule = params ? params->bootstrapModule
                              : GetBootstrapModule();

    wchar_t modulePath[MAX_PATH] {};
    GetModuleFileNameW(bsModule, modulePath, MAX_PATH);
    std::filesystem::path bootstrapDir =
        std::filesystem::path { modulePath }.parent_path();
    const std::filesystem::path runtimePath =
        bootstrapDir / L"runtime.dll";

    const auto loadResult = LoadRuntime(runtimePath);
    if (loadResult.status != StatusCode::Ok) {
        result.status = loadResult.status;
        return result;
    }
    result.runtimeLoaded = true;

    // Step 4: Call RuntimeInitialize from runtime.dll
    using RuntimeInitFn = void* (*)(const void*);
    const auto runtimeInit = reinterpret_cast<RuntimeInitFn>(
        GetProcAddress(loadResult.runtimeModule, "RuntimeInitialize")
    );
    if (runtimeInit) {
        runtimeInit(nullptr);
        result.runtimeInitialized = true;
    } else {
        result.status = StatusCode::RuntimeInitFailed;
    }

    // Notify launcher via IPC
    if (ipc.IsConnected()) {
        BootstrapReadyMessage readyMsg {};
        readyMsg.status = result.status;
        readyMsg.hostModuleName = hostResult.hostModuleName;
        readyMsg.hostVersion = hostResult.hostVersion;
        ipc.SendBootstrapReady(readyMsg);
    }

    return result;
}

} // namespace z3lx::bootstrap
