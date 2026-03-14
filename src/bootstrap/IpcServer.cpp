#include "bootstrap/IpcServer.hpp"

#include <cstdint>
#include <format>
#include <string>

#include <Windows.h>

namespace z3lx::bootstrap {

IpcServer::IpcServer(const uint32_t gamePid) noexcept
    : gamePid { gamePid } {}

IpcServer::~IpcServer() noexcept {
    Close();
}

StatusCode IpcServer::Create() {
    const std::string pipeName = std::format("{}{}", kPipeNamePrefix, gamePid);

    pipeHandle = CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
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

    OVERLAPPED overlapped {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        return StatusCode::IpcDisconnected;
    }

    const BOOL connected = ConnectNamedPipe(pipeHandle, &overlapped);
    if (connected) {
        CloseHandle(overlapped.hEvent);
        return StatusCode::Ok;
    }

    const DWORD lastError = GetLastError();
    if (lastError == ERROR_PIPE_CONNECTED) {
        CloseHandle(overlapped.hEvent);
        return StatusCode::Ok;
    }

    if (lastError == ERROR_IO_PENDING) {
        const DWORD waitResult = WaitForSingleObject(
            overlapped.hEvent, timeoutMs);
        CloseHandle(overlapped.hEvent);
        if (waitResult == WAIT_OBJECT_0) {
            DWORD bytesTransferred = 0;
            if (GetOverlappedResult(
                    pipeHandle, &overlapped, &bytesTransferred, FALSE)) {
                return StatusCode::Ok;
            }
        }
        CancelIo(pipeHandle);
        return StatusCode::IpcDisconnected;
    }

    CloseHandle(overlapped.hEvent);
    return StatusCode::IpcDisconnected;
}

template <typename T>
StatusCode IpcServer::SendPayload(
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
StatusCode IpcServer::ReceivePayload(
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

StatusCode IpcServer::ReceiveHello(HelloMessage& msg) {
    return ReceivePayload(MessageType::Hello, msg);
}

StatusCode IpcServer::SendBootstrapReady(const BootstrapReadyMessage& msg) {
    return SendPayload(MessageType::BootstrapReady, msg);
}

StatusCode IpcServer::ReceiveRuntimeLoadRequest(
    RuntimeLoadRequestMessage& request) {
    return ReceivePayload(MessageType::RuntimeLoadRequest, request);
}

StatusCode IpcServer::ReceiveConfigSnapshot(ConfigSnapshotMessage& config) {
    return ReceivePayload(MessageType::ConfigSnapshot, config);
}

StatusCode IpcServer::SendRuntimeInitResult(
    const RuntimeInitResultMessage& result) {
    return SendPayload(MessageType::RuntimeInitResult, result);
}

StatusCode IpcServer::SendConfigApplyResult(
    const ConfigApplyResultMessage& result) {
    return SendPayload(MessageType::ConfigApplyResult, result);
}

StatusCode IpcServer::SendControlPlaneReady(
    const ControlPlaneReadyMessage& result) {
    return SendPayload(MessageType::ControlPlaneReady, result);
}

StatusCode IpcServer::SendHeartbeat(
    const StatusHeartbeatMessage& heartbeat) {
    return SendPayload(MessageType::StatusHeartbeat, heartbeat);
}

StatusCode IpcServer::SendHookStateChanged(
    const HookStateChangedMessage& hookState) {
    return SendPayload(MessageType::HookStateChanged, hookState);
}

StatusCode IpcServer::SendError(const ErrorEventMessage& error) {
    return SendPayload(MessageType::ErrorEvent, error);
}

bool IpcServer::IsConnected() const noexcept {
    return pipeHandle != INVALID_HANDLE_VALUE;
}

HANDLE IpcServer::ReleaseHandle() noexcept {
    const HANDLE h = pipeHandle;
    pipeHandle = INVALID_HANDLE_VALUE;
    return h;
}

void IpcServer::Close() noexcept {
    if (pipeHandle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(pipeHandle);
        DisconnectNamedPipe(pipeHandle);
        CloseHandle(pipeHandle);
        pipeHandle = INVALID_HANDLE_VALUE;
    }
}

} // namespace z3lx::bootstrap
