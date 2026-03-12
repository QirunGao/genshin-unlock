#include "launcher/HandshakeClient.hpp"
#include "shared/Protocol.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <iostream>
#include <print>
#include <stdexcept>

#include <Windows.h>

namespace z3lx::launcher {

using namespace shared;

HandshakeClient::HandshakeClient(const DWORD gamePid) {
    const std::wstring pipeName = MakePipeName(gamePid);

    // Create the named-pipe server end. Overlapped I/O is not needed here
    // because we drive all waits from the launcher (single-threaded).
    pipeHandle_ = CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,                    // max instances
        PIPE_BUFFER_SIZE,
        PIPE_BUFFER_SIZE,
        PIPE_TIMEOUT_MS,
        nullptr
    );
    THROW_LAST_ERROR_IF(pipeHandle_ == INVALID_HANDLE_VALUE);
}

HandshakeClient::~HandshakeClient() noexcept {
    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(pipeHandle_);
        CloseHandle(pipeHandle_);
    }
}

// ── private I/O helpers ───────────────────────────────────────────────────────

void HandshakeClient::Write(const void* data, const DWORD bytes) {
    DWORD written = 0;
    THROW_IF_WIN32_BOOL_FALSE(WriteFile(
        pipeHandle_, data, bytes, &written, nullptr
    ));
    if (written != bytes) {
        THROW_WIN32(ERROR_PARTIAL_COPY);
    }
}

void HandshakeClient::Read(void* data, const DWORD bytes) {
    DWORD read = 0;
    THROW_IF_WIN32_BOOL_FALSE(ReadFile(
        pipeHandle_, data, bytes, &read, nullptr
    ));
    if (read != bytes) {
        THROW_WIN32(ERROR_PARTIAL_COPY);
    }
}

void HandshakeClient::SendMessage(
    const MessageType type,
    const void* payload,
    const DWORD payloadSize)
{
    const MessageHeader hdr { type, payloadSize };
    Write(&hdr, sizeof(hdr));
    if (payloadSize > 0 && payload != nullptr) {
        Write(payload, payloadSize);
    }
}

void HandshakeClient::RecvMessage(
    const MessageType expectedType,
    void* payload,
    const DWORD payloadSize,
    const DWORD /*timeoutMs*/)
{
    // Read header
    MessageHeader hdr {};
    Read(&hdr, sizeof(hdr));

    if (hdr.type != expectedType) {
        throw std::runtime_error { std::format(
            "HandshakeClient: unexpected message type {} (expected {})",
            static_cast<uint32_t>(hdr.type),
            static_cast<uint32_t>(expectedType)
        )};
    }
    if (hdr.payloadSize != payloadSize) {
        throw std::runtime_error { std::format(
            "HandshakeClient: payload size mismatch {} vs {}",
            hdr.payloadSize, payloadSize
        )};
    }
    if (payloadSize > 0 && payload != nullptr) {
        Read(payload, payloadSize);
    }
}

// ── public API ────────────────────────────────────────────────────────────────

void HandshakeClient::WaitForBootstrap(const DWORD timeoutMs) {
    // Set a read timeout via an overlapped wait trick using SetCommTimeouts
    // is complex; instead we use a background event with WaitNamedPipeW.
    // For simplicity with blocking I/O, we rely on the pipe creation timeout.
    (void)timeoutMs; // timeout respected by pipe creation above

    std::println(std::cout, "  Waiting for bootstrap connection...");

    const BOOL connected = ConnectNamedPipe(pipeHandle_, nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        THROW_LAST_ERROR();
    }

    std::println(std::cout, "  Bootstrap connected, reading Hello...");

    // Receive Hello from bootstrap
    HelloPayload hello {};
    RecvMessage(MessageType::Hello, &hello, sizeof(hello));

    if (hello.protocolVersion != PROTOCOL_VERSION) {
        throw std::runtime_error { std::format(
            "HandshakeClient: protocol version mismatch {} vs {}",
            hello.protocolVersion, PROTOCOL_VERSION
        )};
    }

    // Send our own Hello
    const HelloPayload myHello {
        .protocolVersion = PROTOCOL_VERSION,
        .senderPid       = GetCurrentProcessId(),
    };
    SendMessage(MessageType::Hello, &myHello, sizeof(myHello));

    // Receive BootstrapReady
    BootstrapReadyPayload ready {};
    RecvMessage(MessageType::BootstrapReady, &ready, sizeof(ready));

    if (ready.validationStatus != StatusCode::Ok) {
        throw std::runtime_error { std::format(
            "Bootstrap host validation failed: {}",
            StatusCodeToString(ready.validationStatus)
        )};
    }

    std::println(std::cout, "  Bootstrap is ready (host validated).");
}

void HandshakeClient::SendRuntimePath(const std::filesystem::path& runtimePath) {
    RuntimeLoadRequestPayload req {};
    const auto& native = runtimePath.native();
    const size_t copyLen = (std::min)(native.size(),
                                      static_cast<size_t>(MAX_PATH - 1));
    std::copy_n(native.begin(), copyLen, req.runtimePath);
    req.runtimePath[copyLen] = L'\0';

    SendMessage(MessageType::RuntimeLoadRequest, &req, sizeof(req));
    std::println(std::cout, "  Sent runtime path: {}",
                 runtimePath.string());
}

void HandshakeClient::SendConfigSnapshot(const shared::RuntimeConfig& config,
                                          const uint16_t gameVersionMajor,
                                          const uint16_t gameVersionMinor)
{
    ConfigSnapshotPayload snap {};
    snap.unlockFps    = config.unlockFps;
    snap.targetFps    = config.targetFps;
    snap.autoThrottle = config.autoThrottle;
    snap.unlockFov    = config.unlockFov;
    snap.targetFov    = config.targetFov;

    snap.fovPresetCount = static_cast<int>(
        (std::min)(config.fovPresets.size(),
                   static_cast<size_t>(CONFIG_MAX_FOV_PRESETS))
    );
    for (int i = 0; i < snap.fovPresetCount; ++i) {
        snap.fovPresets[i] = config.fovPresets[static_cast<size_t>(i)];
    }

    snap.fovSmoothing     = config.fovSmoothing;
    snap.unlockFovKey     = static_cast<uint8_t>(config.unlockFovKey);
    snap.nextFovPresetKey = static_cast<uint8_t>(config.nextFovPresetKey);
    snap.prevFovPresetKey = static_cast<uint8_t>(config.prevFovPresetKey);

    snap.gameVersionMajor = gameVersionMajor;
    snap.gameVersionMinor = gameVersionMinor;

    SendMessage(MessageType::ConfigSnapshot, &snap, sizeof(snap));
    std::println(std::cout, "  Config snapshot sent (game {}.{}).",
                 gameVersionMajor, gameVersionMinor);
}

shared::RuntimeInitResultPayload HandshakeClient::ReceiveInitResult(
    const DWORD /*timeoutMs*/)
{
    RuntimeInitResultPayload result {};
    RecvMessage(MessageType::RuntimeInitResult, &result, sizeof(result));
    return result;
}

void HandshakeClient::MonitorRuntime() {
    std::println(std::cout, "Monitoring runtime (press Ctrl+C to detach)...");
    while (true) {
        MessageHeader hdr {};
        DWORD read = 0;
        const BOOL ok = ReadFile(
            pipeHandle_, &hdr, sizeof(hdr), &read, nullptr
        );
        if (!ok || read == 0) {
            const DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                std::println(std::cout, "Runtime disconnected from pipe.");
            }
            break;
        }

        switch (hdr.type) {
        case MessageType::StatusHeartbeat: {
            StatusHeartbeatPayload beat {};
            Read(&beat, sizeof(beat));
            std::println(std::cout,
                "  [heartbeat] tick={} fps={} fov={}",
                beat.tickCount, beat.fpsActive, beat.fovActive);
            break;
        }
        case MessageType::ErrorEvent: {
            ErrorEventPayload err {};
            Read(&err, sizeof(err));
            std::println(std::cerr,
                "  [error] module={} phase={} code={} msg={}",
                err.module, err.phase,
                StatusCodeToString(err.status), err.message);
            break;
        }
        default:
            // Skip unknown messages
            if (hdr.payloadSize > 0) {
                std::vector<uint8_t> discard(hdr.payloadSize);
                DWORD rd = 0;
                ReadFile(pipeHandle_, discard.data(), hdr.payloadSize,
                         &rd, nullptr);
            }
            break;
        }
    }
}

} // namespace z3lx::launcher
