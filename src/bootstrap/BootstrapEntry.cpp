#include "bootstrap/BootstrapEntry.hpp"
#include "bootstrap/HostValidator.hpp"
#include "bootstrap/IpcServer.hpp"
#include "bootstrap/RuntimeLoader.hpp"
#include "runtime/RuntimeMain.hpp"
#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include <Windows.h>

namespace z3lx::bootstrap {

// Persistent IPC server — heap-allocated so the pipe handle stays valid
// for the runtime's background thread after BootstrapInitialize returns.
static IpcServer* g_persistentIpc = nullptr;

namespace {

struct BootstrapErrorDetail {
    StatusCode status = StatusCode::Ok;
    uint32_t systemError = 0;
    std::string_view phaseName = "bootstrap_init";
    std::string message;
};

template <typename T>
bool WriteMessageRaw(
    const HANDLE pipeHandle, const MessageType type, const T& payload) {
    const MessageHeader header {
        .type = type,
        .payloadSize = static_cast<uint32_t>(sizeof(T))
    };
    DWORD written = 0;
    return WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr) &&
           written == sizeof(header) &&
           WriteFile(pipeHandle, &payload, sizeof(payload), &written, nullptr) &&
           written == sizeof(payload);
}

bool IsNonEmptyFixedString(const char* value) noexcept {
    return value && value[0] != '\0';
}

bool IsProcessAlive(const DWORD processId, uint32_t& systemError) noexcept {
    const HANDLE process = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processId
    );
    if (!process) {
        systemError = GetLastError();
        return false;
    }

    const HANDLE rawHandle = process;
    const DWORD waitResult = WaitForSingleObject(rawHandle, 0);
    CloseHandle(rawHandle);
    return waitResult == WAIT_TIMEOUT;
}

BootstrapErrorDetail ValidateHelloSession(
    const HelloMessage& hello, const uint32_t expectedGamePid) noexcept {
    if (hello.session.protocolVersion != kProtocolVersion) {
        return BootstrapErrorDetail {
            .status = StatusCode::ProtocolMismatch,
            .phaseName = "hello_validation",
            .message = std::format(
                "Protocol version mismatch. Expected {}, got {}.",
                kProtocolVersion,
                hello.session.protocolVersion)
        };
    }
    if (hello.session.launcherPid == 0) {
        return BootstrapErrorDetail {
            .status = StatusCode::ProtocolMismatch,
            .phaseName = "hello_validation",
            .message = "launcherPid must be non-zero."
        };
    }
    if (hello.session.gamePid != expectedGamePid) {
        return BootstrapErrorDetail {
            .status = StatusCode::ProtocolMismatch,
            .phaseName = "hello_validation",
            .message = std::format(
                "Game PID mismatch. Expected {}, got {}.",
                expectedGamePid,
                hello.session.gamePid)
        };
    }
    if (hello.session.sessionId != hello.session.gamePid) {
        return BootstrapErrorDetail {
            .status = StatusCode::ProtocolMismatch,
            .phaseName = "hello_validation",
            .message = "sessionId must match gamePid for this transport."
        };
    }
    if (!IsNonEmptyFixedString(hello.session.toolVersion)) {
        return BootstrapErrorDetail {
            .status = StatusCode::ProtocolMismatch,
            .phaseName = "hello_validation",
            .message = "toolVersion must be present in Hello."
        };
    }
    if (!IsNonEmptyFixedString(hello.session.gameVersion)) {
        return BootstrapErrorDetail {
            .status = StatusCode::ProtocolMismatch,
            .phaseName = "hello_validation",
            .message = "gameVersion must be present in Hello."
        };
    }

    uint32_t systemError = 0;
    if (!IsProcessAlive(hello.session.launcherPid, systemError)) {
        return BootstrapErrorDetail {
            .status = systemError == 0
                ? StatusCode::ProtocolMismatch
                : StatusCode::BootstrapInitFailed,
            .systemError = systemError,
            .phaseName = "hello_validation",
            .message = systemError == 0
                ? "Launcher process exited before bootstrap handshake completed."
                : "Failed to inspect launcher process during bootstrap handshake."
        };
    }

    return BootstrapErrorDetail {};
}

BootstrapReadyMessage MakeBootstrapReadyMessage(
    const StatusCode status,
    const std::string_view phaseName,
    const std::string_view message,
    const uint32_t systemError = 0,
    const HostValidationResult* hostResult = nullptr) noexcept {
    BootstrapReadyMessage ready {};
    ready.status = status;
    ready.systemError = systemError;
    CopyToFixedString(ready.phaseName, sizeof(ready.phaseName),
        std::string { phaseName }.c_str());
    CopyToFixedString(ready.message, sizeof(ready.message),
        std::string { message }.c_str());
    if (hostResult) {
        CopyToFixedString(ready.hostModuleName, sizeof(ready.hostModuleName),
            hostResult->hostModuleName.c_str());
        CopyToFixedString(ready.hostVersion, sizeof(ready.hostVersion),
            hostResult->hostVersion.c_str());
    }
    return ready;
}

RuntimeInitResultMessage MakeRuntimeInitResultMessage(
    const StatusCode status,
    const std::string_view phaseName,
    const std::string_view message,
    const uint32_t systemError = 0) noexcept {
    RuntimeInitResultMessage result {};
    result.status = status;
    result.systemError = systemError;
    CopyToFixedString(result.phaseName, sizeof(result.phaseName),
        std::string { phaseName }.c_str());
    CopyToFixedString(result.message, sizeof(result.message),
        std::string { message }.c_str());
    return result;
}

} // namespace

extern HMODULE GetBootstrapModule() noexcept;

extern "C" __declspec(dllexport)
DWORD WINAPI BootstrapEntryPoint(LPVOID /*param*/) {
    const auto result = BootstrapInitialize(nullptr);
    return static_cast<DWORD>(result.status);
}

extern "C" __declspec(dllexport)
BootstrapInitResult BootstrapInitialize(const BootstrapInitParams* params) {
    BootstrapInitResult result {};

    // Step 1: Establish IPC and receive Hello so bootstrap failures can be
    //         reported explicitly instead of surfacing as an opaque timeout.
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

    // Step 2: Receive and validate Hello from launcher
    HelloMessage hello {};
    if (ipc->ReceiveHello(hello) != StatusCode::Ok) {
        delete ipc;
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    const auto sessionValidation = ValidateHelloSession(hello, gamePid);
    if (sessionValidation.status != StatusCode::Ok) {
        const BootstrapReadyMessage readyMsg = MakeBootstrapReadyMessage(
            sessionValidation.status,
            sessionValidation.phaseName,
            sessionValidation.message,
            sessionValidation.systemError);
        (void)ipc->SendBootstrapReady(readyMsg);
        delete ipc;
        result.status = sessionValidation.status;
        return result;
    }

    // Step 3: Validate host process and acknowledge bootstrap readiness
    const auto hostResult = ValidateHost();
    if (hostResult.status != StatusCode::Ok) {
        const BootstrapReadyMessage readyMsg = MakeBootstrapReadyMessage(
            hostResult.status,
            hostResult.phaseName,
            hostResult.message,
            hostResult.systemError);
        (void)ipc->SendBootstrapReady(readyMsg);
        delete ipc;
        result.status = hostResult.status;
        return result;
    }

    const BootstrapReadyMessage readyMsg = MakeBootstrapReadyMessage(
        StatusCode::Ok,
        hostResult.phaseName,
        hostResult.message,
        0,
        &hostResult);
    if (ipc->SendBootstrapReady(readyMsg) != StatusCode::Ok) {
        delete ipc;
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 4: Receive runtime load request and validate path/hash
    RuntimeLoadRequestMessage loadRequest {};
    if (ipc->ReceiveRuntimeLoadRequest(loadRequest) != StatusCode::Ok) {
        delete ipc;
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 5: Receive ConfigSnapshot from launcher
    ConfigSnapshotMessage config {};
    if (ipc->ReceiveConfigSnapshot(config) != StatusCode::Ok) {
        delete ipc;
        result.status = StatusCode::IpcDisconnected;
        return result;
    }

    // Step 6: Load runtime.dll from the bootstrap directory after
    //         confirming the launcher requested the expected module.
    HMODULE bsModule = params ? params->bootstrapModule
                              : GetBootstrapModule();

    wchar_t modulePath[MAX_PATH] {};
    GetModuleFileNameW(bsModule, modulePath, MAX_PATH);
    std::filesystem::path bootstrapDir =
        std::filesystem::path { modulePath }.parent_path();
    const std::filesystem::path runtimePath =
        bootstrapDir / L"runtime.dll";
    const std::filesystem::path requestedRuntimePath {
        std::filesystem::path { loadRequest.runtimePath }
    };
    if (requestedRuntimePath != runtimePath) {
        result.status = StatusCode::ModuleSignatureInvalid;
        const RuntimeInitResultMessage initMsg = MakeRuntimeInitResultMessage(
            result.status,
            "runtime_load",
            "Launcher requested an unexpected runtime path.");
        ipc->SendRuntimeInitResult(initMsg);
        delete ipc;
        return result;
    }

    const auto loadResult = LoadRuntime(
        runtimePath, std::string { loadRequest.runtimeSha256 });
    if (loadResult.status != StatusCode::Ok) {
        result.status = loadResult.status;
        const RuntimeInitResultMessage initMsg = MakeRuntimeInitResultMessage(
            loadResult.status,
            loadResult.phaseName,
            loadResult.message,
            loadResult.systemError);
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
    using RuntimeStartFn = runtime::ControlPlaneStartResult (*)();
    const auto runtimeInit = reinterpret_cast<RuntimeInitFn>(
        GetProcAddress(loadResult.runtimeModule, "RuntimeInitialize")
    );
    const auto runtimeStart = reinterpret_cast<RuntimeStartFn>(
        GetProcAddress(loadResult.runtimeModule, "RuntimeStartControlPlane")
    );

    RuntimeInitResultMessage initResultMsg {};
    if (runtimeInit) {
        // Release the pipe handle so bootstrap and runtime share the same
        // connected endpoint during the handoff; runtime does not start
        // background IPC activity until RuntimeStartControlPlane is called.
        const HANDLE pipeHandle = ipc->ReleaseHandle();

        runtime::RuntimeInitParams runtimeParams {};
        runtimeParams.config = config;
        runtimeParams.gamePid = gamePid;
        CopyToFixedString(runtimeParams.gameVersion,
            sizeof(runtimeParams.gameVersion),
            hello.session.gameVersion);
        runtimeParams.ipcPipeHandle = static_cast<void*>(pipeHandle);

        const auto runtimeResult = runtimeInit(&runtimeParams);
        initResultMsg.status = runtimeResult.status;
        initResultMsg.fpsAvailable = runtimeResult.fpsAvailable ? 1u : 0u;
        initResultMsg.fovAvailable = runtimeResult.fovAvailable ? 1u : 0u;
        initResultMsg.runtimeState = runtimeResult.runtimeState;
        initResultMsg.systemError = runtimeResult.systemError;
        CopyToFixedString(initResultMsg.phaseName,
            sizeof(initResultMsg.phaseName), runtimeResult.phaseName);
        CopyToFixedString(initResultMsg.message,
            sizeof(initResultMsg.message), runtimeResult.message);
        result.runtimeInitialized =
            (runtimeResult.status == StatusCode::Ok);

        const bool initSent = WriteMessageRaw(
            pipeHandle, MessageType::RuntimeInitResult, initResultMsg);

        ConfigApplyResultMessage configApplyMsg {};
        configApplyMsg.status = runtimeResult.status;
        configApplyMsg.configVersion = config.version;
        const bool configSent = WriteMessageRaw(
            pipeHandle, MessageType::ConfigApplyResult, configApplyMsg);

        if (!initSent || !configSent) {
            result.status = StatusCode::IpcDisconnected;
            CloseHandle(pipeHandle);
            delete ipc;
            return result;
        }

        if (runtimeResult.status == StatusCode::Ok) {
            if (!runtimeStart) {
                result.status = StatusCode::RuntimeInitFailed;
                ControlPlaneReadyMessage controlPlaneMsg {};
                controlPlaneMsg.status = result.status;
                controlPlaneMsg.runtimeState = runtimeResult.runtimeState;
                CopyToFixedString(controlPlaneMsg.phaseName,
                    sizeof(controlPlaneMsg.phaseName),
                    "control_plane_start");
                CopyToFixedString(controlPlaneMsg.message,
                    sizeof(controlPlaneMsg.message),
                    "RuntimeStartControlPlane export was not found.");
                (void)WriteMessageRaw(
                    pipeHandle, MessageType::ControlPlaneReady, controlPlaneMsg);
                CloseHandle(pipeHandle);
                delete ipc;
                return result;
            }

            const auto controlPlaneResult = runtimeStart();
            if (controlPlaneResult.status != StatusCode::Ok) {
                result.status = controlPlaneResult.status;
                if (controlPlaneResult.status != StatusCode::IpcDisconnected) {
                    ControlPlaneReadyMessage controlPlaneMsg {};
                    controlPlaneMsg.status = controlPlaneResult.status;
                    controlPlaneMsg.runtimeState =
                        controlPlaneResult.runtimeState;
                    controlPlaneMsg.systemError =
                        controlPlaneResult.systemError;
                    CopyToFixedString(controlPlaneMsg.phaseName,
                        sizeof(controlPlaneMsg.phaseName),
                        controlPlaneResult.phaseName);
                    CopyToFixedString(controlPlaneMsg.message,
                        sizeof(controlPlaneMsg.message),
                        controlPlaneResult.message);
                    (void)WriteMessageRaw(
                        pipeHandle, MessageType::ControlPlaneReady,
                        controlPlaneMsg);
                }
                CloseHandle(pipeHandle);
                delete ipc;
                return result;
            }
        } else {
            result.status = runtimeResult.status;
        }
    } else {
        result.status = StatusCode::RuntimeInitFailed;
        initResultMsg = MakeRuntimeInitResultMessage(
            StatusCode::RuntimeInitFailed,
            "runtime_load",
            "RuntimeInitialize export was not found.");
        ipc->SendRuntimeInitResult(initResultMsg);
    }

    // Keep IpcServer alive to prevent premature cleanup.
    g_persistentIpc = ipc;

    return result;
}

} // namespace z3lx::bootstrap
