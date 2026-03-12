#include "bootstrap/BootstrapCore.hpp"
#include "common/Constants.hpp"
#include "shared/Protocol.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <print>
#include <stdexcept>

#include <Windows.h>

using FnRuntimeInitialize =
    z3lx::shared::RuntimeInitResult(__cdecl*)(
        const z3lx::shared::RuntimeInitParams*);

// ── I/O helpers on a blocking pipe handle ─────────────────────────────────────

namespace {
using namespace z3lx::shared;

void PipeWrite(const HANDLE h, const void* data, const DWORD bytes) {
    DWORD written = 0;
    THROW_IF_WIN32_BOOL_FALSE(WriteFile(h, data, bytes, &written, nullptr));
    if (written != bytes) {
        THROW_WIN32(ERROR_PARTIAL_COPY);
    }
}

void PipeRead(const HANDLE h, void* data, const DWORD bytes) {
    DWORD read = 0;
    THROW_IF_WIN32_BOOL_FALSE(ReadFile(h, data, bytes, &read, nullptr));
    if (read != bytes) {
        THROW_WIN32(ERROR_PARTIAL_COPY);
    }
}

void SendMsg(const HANDLE h, const MessageType type,
             const void* payload, const DWORD size) {
    const MessageHeader hdr { type, size };
    PipeWrite(h, &hdr, sizeof(hdr));
    if (size > 0 && payload) {
        PipeWrite(h, payload, size);
    }
}

template <typename T>
void RecvMsg(const HANDLE h, const MessageType expectedType, T& payload) {
    MessageHeader hdr {};
    PipeRead(h, &hdr, sizeof(hdr));
    if (hdr.type != expectedType) {
        throw std::runtime_error { std::format(
            "bootstrap: unexpected message type {} (expected {})",
            static_cast<uint32_t>(hdr.type),
            static_cast<uint32_t>(expectedType)
        )};
    }
    if (hdr.payloadSize != sizeof(T)) {
        throw std::runtime_error { std::format(
            "bootstrap: payload size mismatch {} vs {}",
            hdr.payloadSize, sizeof(T)
        )};
    }
    PipeRead(h, &payload, sizeof(T));
}

// ── Host validation ───────────────────────────────────────────────────────────

StatusCode ValidateHost() noexcept {
    using namespace z3lx::common;
    const HMODULE osModule = GetModuleHandleW(osGameFileName);
    const HMODULE cnModule = GetModuleHandleW(cnGameFileName);
    if (osModule == nullptr && cnModule == nullptr) {
        return StatusCode::ModuleSignatureInvalid;
    }
    return StatusCode::Ok;
}

} // namespace

// ── RunBootstrap ──────────────────────────────────────────────────────────────

namespace z3lx::bootstrap {

void RunBootstrap(const HMODULE /* hModule */) {
    // Derive the pipe name from our own process ID (== game process ID)
    const std::wstring pipeName = shared::MakePipeName(GetCurrentProcessId());

    // Connect to the launcher-side pipe server
    for (int retries = 10; ; --retries) {
        const BOOL ok = WaitNamedPipeW(pipeName.c_str(),
                                       shared::PIPE_TIMEOUT_MS);
        if (ok) {
            break;
        }
        if (retries == 0) {
            THROW_LAST_ERROR();
        }
        Sleep(200);
    }

    const wil::unique_hfile pipe {
        CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        )
    };
    THROW_LAST_ERROR_IF(!pipe.is_valid());

    // Switch to message-read mode
    DWORD mode = PIPE_READMODE_MESSAGE;
    THROW_IF_WIN32_BOOL_FALSE(SetNamedPipeHandleState(
        pipe.get(), &mode, nullptr, nullptr
    ));

    // ── Hello exchange ────────────────────────────────────────────────────────
    const shared::HelloPayload myHello {
        .protocolVersion = shared::PROTOCOL_VERSION,
        .senderPid       = GetCurrentProcessId(),
    };
    SendMsg(pipe.get(), shared::MessageType::Hello,
            &myHello, sizeof(myHello));

    shared::HelloPayload launcherHello {};
    RecvMsg(pipe.get(), shared::MessageType::Hello, launcherHello);

    if (launcherHello.protocolVersion != shared::PROTOCOL_VERSION) {
        throw std::runtime_error { std::format(
            "bootstrap: launcher protocol version mismatch {} vs {}",
            launcherHello.protocolVersion, shared::PROTOCOL_VERSION
        )};
    }

    // ── Host validation ───────────────────────────────────────────────────────
    const shared::StatusCode hostStatus = ValidateHost();
    {
        const shared::BootstrapReadyPayload ready { .validationStatus = hostStatus };
        SendMsg(pipe.get(), shared::MessageType::BootstrapReady,
                &ready, sizeof(ready));
    }

    if (hostStatus != shared::StatusCode::Ok) {
        return; // Launcher will handle the failure
    }

    // ── Receive runtime.dll path ──────────────────────────────────────────────
    shared::RuntimeLoadRequestPayload loadReq {};
    RecvMsg(pipe.get(), shared::MessageType::RuntimeLoadRequest, loadReq);

    const std::filesystem::path runtimePath { loadReq.runtimePath };

    // ── Receive config snapshot ───────────────────────────────────────────────
    shared::ConfigSnapshotPayload config {};
    RecvMsg(pipe.get(), shared::MessageType::ConfigSnapshot, config);

    // ── Load runtime.dll ──────────────────────────────────────────────────────
    const wil::unique_hmodule runtimeModule {
        LoadLibraryW(runtimePath.c_str())
    };
    if (!runtimeModule) {
        const shared::RuntimeInitResultPayload fail {
            .status       = shared::StatusCode::RuntimeLoadFailed,
            .fpsAvailable = false,
            .fovAvailable = false,
        };
        SendMsg(pipe.get(), shared::MessageType::RuntimeInitResult,
                &fail, sizeof(fail));
        return;
    }

    // ── Call RuntimeInitialize ────────────────────────────────────────────────
    const auto initFn = reinterpret_cast<FnRuntimeInitialize>(
        GetProcAddress(runtimeModule.get(), "RuntimeInitialize")
    );
    if (!initFn) {
        const shared::RuntimeInitResultPayload fail {
            .status       = shared::StatusCode::RuntimeInitFailed,
            .fpsAvailable = false,
            .fovAvailable = false,
        };
        SendMsg(pipe.get(), shared::MessageType::RuntimeInitResult,
                &fail, sizeof(fail));
        return;
    }

    const z3lx::shared::RuntimeInitParams params {
        .config            = config,
        .gameVersionMajor  = config.gameVersionMajor,
        .gameVersionMinor  = config.gameVersionMinor,
    };
    const z3lx::shared::RuntimeInitResult result = initFn(&params);

    const shared::RuntimeInitResultPayload resultPayload {
        .status       = result.status,
        .fpsAvailable = result.fpsAvailable,
        .fovAvailable = result.fovAvailable,
    };
    SendMsg(pipe.get(), shared::MessageType::RuntimeInitResult,
            &resultPayload, sizeof(resultPayload));

    if (result.status != shared::StatusCode::Ok) {
        return;
    }

    // ── Keep-alive: forward heartbeat messages from runtime ───────────────────
    // (Runtime posts them via a shared callback installed during init)
    // For now we simply keep the pipe open; the launcher closes on its side.
    while (true) {
        MessageHeader hdr {};
        DWORD         rd = 0;
        const BOOL    ok = ReadFile(pipe.get(), &hdr, sizeof(hdr), &rd, nullptr);
        if (!ok || rd == 0) {
            break;
        }
        // Forward verbatim (we only handle ShutdownRequest specially)
        if (hdr.type == shared::MessageType::ShutdownRequest) {
            break;
        }
    }

    // runtimeModule handle is intentionally not freed here; the DLL must
    // remain loaded for the duration of the game process.
    runtimeModule.release();
}

} // namespace z3lx::bootstrap
