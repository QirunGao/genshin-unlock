#include "bootstrap/IpcServer.hpp"

#include <format>
#include <string>

#include <Windows.h>

namespace z3lx::bootstrap {

IpcServer::IpcServer(const uint32_t gamePid) noexcept
    : gamePid { gamePid }
    , pipeName { std::format("{}{}",
        kPipeNamePrefix, gamePid) } {}

IpcServer::~IpcServer() noexcept {
    Close();
}

StatusCode IpcServer::Create() {
    pipeHandle = CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        4096,
        4096,
        5000,
        nullptr
    );
    if (pipeHandle == INVALID_HANDLE_VALUE) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::WaitForConnection(const uint32_t timeoutMs) {
    if (pipeHandle == INVALID_HANDLE_VALUE) {
        return StatusCode::IpcDisconnected;
    }
    if (!ConnectNamedPipe(pipeHandle, nullptr)) {
        const DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            return StatusCode::IpcDisconnected;
        }
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::ReceiveHello(HelloMessage& msg) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {};
    DWORD bytesRead = 0;
    if (!ReadFile(pipeHandle, &header, sizeof(header), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    if (header.type != MessageType::Hello) {
        return StatusCode::IpcDisconnected;
    }

    uint32_t protocolVersion = 0;
    if (!ReadFile(pipeHandle, &protocolVersion, sizeof(protocolVersion), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    msg.session.protocolVersion = protocolVersion;
    return StatusCode::Ok;
}

StatusCode IpcServer::SendBootstrapReady(const BootstrapReadyMessage& msg) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::BootstrapReady,
        .payloadSize = sizeof(uint32_t)
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    const uint32_t statusVal = static_cast<uint32_t>(msg.status);
    if (!WriteFile(pipeHandle, &statusVal, sizeof(statusVal), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::ReceiveConfigSnapshot(ConfigSnapshotMessage& config) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {};
    DWORD bytesRead = 0;
    if (!ReadFile(pipeHandle, &header, sizeof(header), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    if (header.type != MessageType::ConfigSnapshot) {
        return StatusCode::IpcDisconnected;
    }
    if (!ReadFile(pipeHandle, &config, sizeof(config), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::SendRuntimeInitResult(const RuntimeInitResultMessage& result) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::RuntimeInitResult,
        .payloadSize = sizeof(uint32_t) * 3
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    struct {
        uint32_t status;
        uint32_t fpsAvailable;
        uint32_t fovAvailable;
    } payload {
        .status = static_cast<uint32_t>(result.status),
        .fpsAvailable = result.fpsAvailable ? 1u : 0u,
        .fovAvailable = result.fovAvailable ? 1u : 0u
    };
    if (!WriteFile(pipeHandle, &payload, sizeof(payload), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::SendHeartbeat(const StatusHeartbeatMessage& heartbeat) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::StatusHeartbeat,
        .payloadSize = sizeof(heartbeat)
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr) ||
        !WriteFile(pipeHandle, &heartbeat, sizeof(heartbeat), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::SendHookStateChanged(const HookStateChangedMessage& hookState) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::HookStateChanged,
        .payloadSize = sizeof(uint32_t) * 3
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    struct {
        uint32_t installed;
        uint32_t enabled;
        uint32_t status;
    } payload {
        .installed = hookState.installed ? 1u : 0u,
        .enabled = hookState.enabled ? 1u : 0u,
        .status = static_cast<uint32_t>(hookState.status)
    };
    if (!WriteFile(pipeHandle, &payload, sizeof(payload), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode IpcServer::SendError(const ErrorEventMessage& error) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::ErrorEvent,
        .payloadSize = sizeof(uint32_t) * 2
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    struct {
        uint32_t code;
        uint32_t systemError;
    } payload {
        .code = static_cast<uint32_t>(error.error.code),
        .systemError = error.error.systemError
    };
    if (!WriteFile(pipeHandle, &payload, sizeof(payload), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

bool IpcServer::IsConnected() const noexcept {
    return pipeHandle != INVALID_HANDLE_VALUE;
}

void IpcServer::Close() noexcept {
    if (pipeHandle != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(pipeHandle);
        CloseHandle(pipeHandle);
        pipeHandle = INVALID_HANDLE_VALUE;
    }
}

} // namespace z3lx::bootstrap
