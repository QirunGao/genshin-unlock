#include "bootstrap/BootstrapEntry.hpp"
#include "bootstrap/HostValidator.hpp"
#include "bootstrap/IpcServer.hpp"
#include "bootstrap/RuntimeLoader.hpp"
#include "runtime/RuntimeMain.hpp"
#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <filesystem>

#include <Windows.h>

namespace z3lx::bootstrap {

extern HMODULE GetBootstrapModule() noexcept;

extern "C" __declspec(dllexport)
DWORD WINAPI BootstrapEntryPoint(LPVOID /*param*/) {
    const auto result = BootstrapInitialize(nullptr);
    return static_cast<DWORD>(result.status);
}

extern "C" __declspec(dllexport)
BootstrapInitResult BootstrapInitialize(const BootstrapInitParams* params) {
    BootstrapInitResult result {};

    // Step 1: Validate host process
    const auto hostResult = ValidateHost();
    if (hostResult.status != StatusCode::Ok) {
        result.status = hostResult.status;
        return result;
    }

    // Step 2: Establish IPC — create pipe and wait for launcher connection
    const uint32_t gamePid = GetCurrentProcessId();
    IpcServer ipc { gamePid };
    if (ipc.Create() != StatusCode::Ok) {
        result.status = StatusCode::IpcDisconnected;
        return result;
    }
    if (ipc.WaitForConnection() != StatusCode::Ok) {
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 3: Receive Hello from launcher
    HelloMessage hello {};
    if (ipc.ReceiveHello(hello) != StatusCode::Ok) {
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 4: Send BootstrapReady (host validated, ready for config)
    BootstrapReadyMessage readyMsg {};
    readyMsg.status = StatusCode::Ok;
    CopyToFixedString(readyMsg.hostModuleName,
        sizeof(readyMsg.hostModuleName),
        hostResult.hostModuleName.c_str());
    CopyToFixedString(readyMsg.hostVersion,
        sizeof(readyMsg.hostVersion),
        hostResult.hostVersion.c_str());
    ipc.SendBootstrapReady(readyMsg);

    // Step 5: Receive ConfigSnapshot from launcher
    ConfigSnapshotMessage config {};
    if (ipc.ReceiveConfigSnapshot(config) != StatusCode::Ok) {
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 6: Load runtime.dll from the bootstrap directory
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
        RuntimeInitResultMessage initMsg {};
        initMsg.status = loadResult.status;
        ipc.SendRuntimeInitResult(initMsg);
        return result;
    }
    result.runtimeLoaded = true;

    // Step 7: Call RuntimeInitialize from runtime.dll with config
    using RuntimeInitFn =
        runtime::RuntimeInitResult (*)(const runtime::RuntimeInitParams*);
    const auto runtimeInit = reinterpret_cast<RuntimeInitFn>(
        GetProcAddress(loadResult.runtimeModule, "RuntimeInitialize")
    );

    RuntimeInitResultMessage initResultMsg {};
    if (runtimeInit) {
        runtime::RuntimeInitParams runtimeParams {};
        runtimeParams.config = config;
        runtimeParams.gamePid = gamePid;

        const auto runtimeResult = runtimeInit(&runtimeParams);
        initResultMsg.status = runtimeResult.status;
        initResultMsg.fpsAvailable = runtimeResult.fpsAvailable ? 1u : 0u;
        initResultMsg.fovAvailable = runtimeResult.fovAvailable ? 1u : 0u;
        result.runtimeInitialized =
            (runtimeResult.status == StatusCode::Ok);
    } else {
        initResultMsg.status = StatusCode::RuntimeInitFailed;
        result.status = StatusCode::RuntimeInitFailed;
    }

    // Step 8: Send RuntimeInitResult to launcher
    ipc.SendRuntimeInitResult(initResultMsg);

    return result;
}

} // namespace z3lx::bootstrap
