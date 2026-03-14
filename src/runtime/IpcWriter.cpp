#include "runtime/IpcWriter.hpp"

#include <memory>
#include <mutex>
#include <utility>

#include <Windows.h>

namespace z3lx::runtime {

IpcWriter::IpcWriter() noexcept
    : mutex { std::make_unique<std::mutex>() } {}

IpcWriter::IpcWriter(const HANDLE pipeHandle) noexcept
    : pipeHandle { pipeHandle }
    , mutex { std::make_unique<std::mutex>() } {}

IpcWriter::~IpcWriter() noexcept = default;

IpcWriter::IpcWriter(IpcWriter&& other) noexcept
    : pipeHandle { other.pipeHandle }
    , mutex { std::move(other.mutex) } {
    other.pipeHandle = INVALID_HANDLE_VALUE;
}

IpcWriter& IpcWriter::operator=(IpcWriter&& other) noexcept {
    if (this != &other) {
        pipeHandle = other.pipeHandle;
        mutex = std::move(other.mutex);
        other.pipeHandle = INVALID_HANDLE_VALUE;
    }
    return *this;
}

void IpcWriter::SetHandle(const HANDLE handle) noexcept {
    std::lock_guard lock { *mutex };
    pipeHandle = handle;
}

template <typename T>
StatusCode IpcWriter::SendPayload(
    const MessageType type, const T& payload) {
    std::lock_guard lock { *mutex };
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
StatusCode IpcWriter::ReceivePayload(
    const MessageType expectedType, T& payload) {
    std::lock_guard lock { *mutex };
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

StatusCode IpcWriter::SendConfigApplyResult(
    const ConfigApplyResultMessage& msg) {
    return SendPayload(MessageType::ConfigApplyResult, msg);
}

StatusCode IpcWriter::SendControlPlaneReady(
    const ControlPlaneReadyMessage& msg) {
    return SendPayload(MessageType::ControlPlaneReady, msg);
}

StatusCode IpcWriter::SendStateChanged(const StateChangedMessage& msg) {
    return SendPayload(MessageType::StateChanged, msg);
}

StatusCode IpcWriter::SendHeartbeat(
    const StatusHeartbeatMessage& msg) {
    return SendPayload(MessageType::StatusHeartbeat, msg);
}

StatusCode IpcWriter::SendHookStateChanged(
    const HookStateChangedMessage& msg) {
    return SendPayload(MessageType::HookStateChanged, msg);
}

StatusCode IpcWriter::SendLogEvent(const LogEventMessage& msg) {
    return SendPayload(MessageType::LogEvent, msg);
}

StatusCode IpcWriter::SendError(const ErrorEventMessage& msg) {
    return SendPayload(MessageType::ErrorEvent, msg);
}

bool IpcWriter::HasPendingData() const noexcept {
    std::lock_guard lock { *mutex };
    if (!IsConnected()) return false;
    DWORD bytesAvailable = 0;
    if (!PeekNamedPipe(pipeHandle, nullptr, 0,
            nullptr, &bytesAvailable, nullptr)) {
        return false;
    }
    return bytesAvailable >= sizeof(MessageHeader);
}

StatusCode IpcWriter::PeekMessageHeader(MessageHeader& header) {
    std::lock_guard lock { *mutex };
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

StatusCode IpcWriter::ReceiveShutdown(ShutdownRequestMessage& msg) {
    return ReceivePayload(MessageType::ShutdownRequest, msg);
}

bool IpcWriter::IsConnected() const noexcept {
    return pipeHandle != INVALID_HANDLE_VALUE;
}

} // namespace z3lx::runtime
