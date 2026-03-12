#include "launcher/HandshakeClient.hpp"

#include <format>
#include <string>

#include <Windows.h>

namespace z3lx::launcher {

HandshakeClient::HandshakeClient(const uint32_t gamePid) noexcept
    : gamePid { gamePid }
    , pipeName { std::format("{}{}",
        kPipeNamePrefix, gamePid) } {}

HandshakeClient::~HandshakeClient() noexcept {
    Disconnect();
}

StatusCode HandshakeClient::Connect(const uint32_t timeoutMs) {
    // Wait for pipe to become available
    if (!WaitNamedPipeA(pipeName.c_str(), timeoutMs)) {
        return StatusCode::IpcDisconnected;
    }

    pipeHandle = CreateFileA(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );
    if (pipeHandle == INVALID_HANDLE_VALUE) {
        return StatusCode::IpcDisconnected;
    }

    // Set pipe to message mode
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipeHandle, &mode, nullptr, nullptr);

    return StatusCode::Ok;
}

StatusCode HandshakeClient::SendHello(const HelloMessage& msg) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::Hello,
        .payloadSize = sizeof(msg)
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr) ||
        !WriteFile(pipeHandle, &msg.session.protocolVersion,
                   sizeof(uint32_t), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode HandshakeClient::WaitForBootstrapReady(
    BootstrapReadyMessage& response, const uint32_t timeoutMs) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {};
    DWORD bytesRead = 0;
    if (!ReadFile(pipeHandle, &header, sizeof(header), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    if (header.type != MessageType::BootstrapReady) {
        return StatusCode::BootstrapInitFailed;
    }

    uint32_t statusVal = 0;
    if (!ReadFile(pipeHandle, &statusVal, sizeof(statusVal), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    response.status = static_cast<StatusCode>(statusVal);
    return StatusCode::Ok;
}

StatusCode HandshakeClient::SendConfigSnapshot(const ConfigSnapshotMessage& config) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::ConfigSnapshot,
        .payloadSize = sizeof(config)
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr) ||
        !WriteFile(pipeHandle, &config, sizeof(config), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode HandshakeClient::WaitForRuntimeInitResult(
    RuntimeInitResultMessage& response, const uint32_t timeoutMs) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {};
    DWORD bytesRead = 0;
    if (!ReadFile(pipeHandle, &header, sizeof(header), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    if (header.type != MessageType::RuntimeInitResult) {
        return StatusCode::RuntimeInitFailed;
    }

    struct {
        uint32_t status;
        uint32_t fpsAvailable;
        uint32_t fovAvailable;
    } payload {};
    if (!ReadFile(pipeHandle, &payload, sizeof(payload), &bytesRead, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    response.status = static_cast<StatusCode>(payload.status);
    response.fpsAvailable = payload.fpsAvailable != 0;
    response.fovAvailable = payload.fovAvailable != 0;
    return StatusCode::Ok;
}

StatusCode HandshakeClient::SendShutdown(const ShutdownRequestMessage& msg) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {
        .type = MessageType::ShutdownRequest,
        .payloadSize = sizeof(msg)
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header), &written, nullptr) ||
        !WriteFile(pipeHandle, &msg, sizeof(msg), &written, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

bool HandshakeClient::IsConnected() const noexcept {
    return pipeHandle != INVALID_HANDLE_VALUE;
}

void HandshakeClient::Disconnect() noexcept {
    if (pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeHandle);
        pipeHandle = INVALID_HANDLE_VALUE;
    }
}

} // namespace z3lx::launcher
