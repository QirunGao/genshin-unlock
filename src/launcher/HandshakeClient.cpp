#include "launcher/HandshakeClient.hpp"

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <thread>

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

template <typename T>
StatusCode HandshakeClient::ReceivePayloadWithTimeout(
    const MessageType expectedType, T& payload, const uint32_t timeoutMs) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;

    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (true) {
        MessageHeader header {};
        DWORD bytesRead = 0;
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(pipeHandle, &header, sizeof(header),
                &bytesRead, &bytesAvailable, nullptr)) {
            return StatusCode::IpcDisconnected;
        }

        if (bytesRead >= sizeof(header)) {
            if (header.type != expectedType || header.payloadSize != sizeof(T)) {
                return StatusCode::IpcDisconnected;
            }
            if (bytesAvailable >= sizeof(header) + sizeof(T)) {
                return ReceivePayload(expectedType, payload);
            }
        }

        if (GetTickCount64() >= deadline) {
            return StatusCode::IpcDisconnected;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

StatusCode HandshakeClient::SendHello(const HelloMessage& msg) {
    return SendPayload(MessageType::Hello, msg);
}

StatusCode HandshakeClient::WaitForBootstrapReady(
    BootstrapReadyMessage& response, const uint32_t timeoutMs) {
    return ReceivePayloadWithTimeout(
        MessageType::BootstrapReady, response, timeoutMs);
}

StatusCode HandshakeClient::SendRuntimeLoadRequest(
    const RuntimeLoadRequestMessage& request) {
    return SendPayload(MessageType::RuntimeLoadRequest, request);
}

StatusCode HandshakeClient::SendConfigSnapshot(
    const ConfigSnapshotMessage& config) {
    return SendPayload(MessageType::ConfigSnapshot, config);
}

StatusCode HandshakeClient::WaitForRuntimeInitResult(
    RuntimeInitResultMessage& response, const uint32_t timeoutMs) {
    return ReceivePayloadWithTimeout(
        MessageType::RuntimeInitResult, response, timeoutMs);
}

StatusCode HandshakeClient::WaitForConfigApplyResult(
    ConfigApplyResultMessage& response, const uint32_t timeoutMs) {
    return ReceivePayloadWithTimeout(
        MessageType::ConfigApplyResult, response, timeoutMs);
}

StatusCode HandshakeClient::WaitForControlPlaneReady(
    ControlPlaneReadyMessage& response, const uint32_t timeoutMs) {
    return ReceivePayloadWithTimeout(
        MessageType::ControlPlaneReady, response, timeoutMs);
}

StatusCode HandshakeClient::SendShutdown(const ShutdownRequestMessage& msg) {
    return SendPayload(MessageType::ShutdownRequest, msg);
}

bool HandshakeClient::HasPendingData() const noexcept {
    if (!IsConnected()) return false;
    DWORD bytesAvailable = 0;
    if (!PeekNamedPipe(pipeHandle, nullptr, 0,
            nullptr, &bytesAvailable, nullptr)) {
        return false;
    }
    return bytesAvailable >= sizeof(MessageHeader);
}

StatusCode HandshakeClient::PeekMessageHeader(MessageHeader& header) {
    if (!IsConnected()) return StatusCode::IpcDisconnected;
    DWORD bytesRead = 0;
    DWORD bytesAvailable = 0;
    if (!PeekNamedPipe(pipeHandle, &header, sizeof(header),
            &bytesRead, &bytesAvailable, nullptr)) {
        return StatusCode::IpcDisconnected;
    }
    if (bytesRead < sizeof(header) ||
        bytesAvailable < sizeof(header) + header.payloadSize) {
        return StatusCode::IpcDisconnected;
    }
    return StatusCode::Ok;
}

StatusCode HandshakeClient::ReceiveHeartbeat(StatusHeartbeatMessage& msg) {
    return ReceivePayload(MessageType::StatusHeartbeat, msg);
}

StatusCode HandshakeClient::ReceiveStateChanged(StateChangedMessage& msg) {
    return ReceivePayload(MessageType::StateChanged, msg);
}

StatusCode HandshakeClient::ReceiveHookStateChanged(
    HookStateChangedMessage& msg) {
    return ReceivePayload(MessageType::HookStateChanged, msg);
}

StatusCode HandshakeClient::ReceiveLogEvent(LogEventMessage& msg) {
    return ReceivePayload(MessageType::LogEvent, msg);
}

StatusCode HandshakeClient::ReceiveError(ErrorEventMessage& msg) {
    return ReceivePayload(MessageType::ErrorEvent, msg);
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
