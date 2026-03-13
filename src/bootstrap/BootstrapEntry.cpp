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

// Persistent IPC server — heap-allocated so the pipe handle stays valid
// for the runtime's background thread after BootstrapInitialize returns.
static IpcServer* g_persistentIpc = nullptr;

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
    auto* ipc = new (std::nothrow) IpcServer { gamePid };
    if (!ipc) {
        result.status = StatusCode::IpcDisconnected;
        return result;
    }
    if (ipc->Create() != StatusCode::Ok) {
        delete ipc;
        result.status = StatusCode::IpcDisconnected;
        return result;
    }
    if (ipc->WaitForConnection() != StatusCode::Ok) {
        delete ipc;
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 3: Receive Hello from launcher
    HelloMessage hello {};
    if (ipc->ReceiveHello(hello) != StatusCode::Ok) {
        delete ipc;
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
    ipc->SendBootstrapReady(readyMsg);

    // Step 5: Receive ConfigSnapshot from launcher
    ConfigSnapshotMessage config {};
    if (ipc->ReceiveConfigSnapshot(config) != StatusCode::Ok) {
        delete ipc;
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
        ipc->SendRuntimeInitResult(initMsg);
        delete ipc;
        return result;
    }
    result.runtimeLoaded = true;

    // Step 7: Call RuntimeInitialize from runtime.dll with config.
    //         Share the pipe handle so the runtime background thread can
    //         send heartbeat / hook-state / error events to the launcher.
    using RuntimeInitFn =
        runtime::RuntimeInitResult (*)(const runtime::RuntimeInitParams*);
    const auto runtimeInit = reinterpret_cast<RuntimeInitFn>(
        GetProcAddress(loadResult.runtimeModule, "RuntimeInitialize")
    );

    RuntimeInitResultMessage initResultMsg {};
    if (runtimeInit) {
        // Release the pipe handle so only the runtime background thread
        // writes to it from now on.  We save the raw handle so we can
        // send RuntimeInitResult immediately after RuntimeInitialize
        // returns — the background loop hasn't started its 1-Hz
        // heartbeat yet, so there is no concurrent write.
        const HANDLE pipeHandle = ipc->ReleaseHandle();

        runtime::RuntimeInitParams runtimeParams {};
        runtimeParams.config = config;
        runtimeParams.gamePid = gamePid;
        runtimeParams.ipcPipeHandle = static_cast<void*>(pipeHandle);

        const auto runtimeResult = runtimeInit(&runtimeParams);
        initResultMsg.status = runtimeResult.status;
        initResultMsg.fpsAvailable = runtimeResult.fpsAvailable ? 1u : 0u;
        initResultMsg.fovAvailable = runtimeResult.fovAvailable ? 1u : 0u;
        result.runtimeInitialized =
            (runtimeResult.status == StatusCode::Ok);

        // Send RuntimeInitResult through the released handle.
        const MessageHeader hdr {
            .type = MessageType::RuntimeInitResult,
            .payloadSize = static_cast<uint32_t>(sizeof(initResultMsg))
        };
        DWORD written = 0;
        WriteFile(pipeHandle, &hdr, sizeof(hdr), &written, nullptr);
        WriteFile(pipeHandle, &initResultMsg,
            sizeof(initResultMsg), &written, nullptr);
    } else {
        initResultMsg.status = StatusCode::RuntimeInitFailed;
        result.status = StatusCode::RuntimeInitFailed;
        ipc->SendRuntimeInitResult(initResultMsg);
    }

    // Keep IpcServer alive to prevent premature cleanup.
    g_persistentIpc = ipc;

    return result;
}

} // namespace z3lx::bootstrap
