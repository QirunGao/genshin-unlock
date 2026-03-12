#include "launcher/HandshakeClient.hpp"

#include <cstdint>
#include <format>
#include <string>

#include <Windows.h>

namespace z3lx::launcher {

HandshakeClient::HandshakeClient(const uint32_t gamePid) noexcept
    : gamePid { gamePid } {}

HandshakeClient::~HandshakeClient() noexcept {
    Disconnect();
}

StatusCode HandshakeClient::Connect(const uint32_t timeoutMs) {
    const std::string pipeName = std::format("{}{}", kPipeNamePrefix, gamePid);

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

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipeHandle, &mode, nullptr, nullptr);

    return StatusCode::Ok;
}

template <typename T>
StatusCode HandshakeClient::SendPayload(
    const MessageType type, const T& payload) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    const MessageHeader header {
        .type = type,
        .payloadSize = static_cast<uint32_t>(sizeof(T))
    };
    DWORD written = 0;
    if (!WriteFile(pipeHandle, &header, sizeof(header),
            &written, nullptr) || written != sizeof(header)) {
        return StatusCode::IpcDisconnected;
    }
    if (!WriteFile(pipeHandle, &payload, sizeof(T),
            &written, nullptr) || written != sizeof(T)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

template <typename T>
StatusCode HandshakeClient::ReceivePayload(
    const MessageType expectedType, T& payload) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    MessageHeader header {};
    DWORD bytesRead = 0;
    if (!ReadFile(pipeHandle, &header, sizeof(header),
            &bytesRead, nullptr) || bytesRead != sizeof(header)) {
        return StatusCode::IpcDisconnected;
    }
    if (header.type != expectedType || header.payloadSize != sizeof(T)) {
        return StatusCode::IpcDisconnected;
    }
    if (!ReadFile(pipeHandle, &payload, sizeof(T),
            &bytesRead, nullptr) || bytesRead != sizeof(T)) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode HandshakeClient::SendHello(const HelloMessage& msg) {
    return SendPayload(MessageType::Hello, msg);
}

StatusCode HandshakeClient::WaitForBootstrapReady(
    BootstrapReadyMessage& response, const uint32_t /*timeoutMs*/) {
    return ReceivePayload(MessageType::BootstrapReady, response);
}

StatusCode HandshakeClient::SendConfigSnapshot(
    const ConfigSnapshotMessage& config) {
    return SendPayload(MessageType::ConfigSnapshot, config);
}

StatusCode HandshakeClient::WaitForRuntimeInitResult(
    RuntimeInitResultMessage& response, const uint32_t /*timeoutMs*/) {
    return ReceivePayload(MessageType::RuntimeInitResult, response);
}

StatusCode HandshakeClient::SendShutdown(const ShutdownRequestMessage& msg) {
    return SendPayload(MessageType::ShutdownRequest, msg);
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
