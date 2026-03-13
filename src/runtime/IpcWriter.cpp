#include "runtime/IpcWriter.hpp"

#include <Windows.h>

namespace z3lx::runtime {

IpcWriter::IpcWriter() noexcept = default;

IpcWriter::IpcWriter(const HANDLE pipeHandle) noexcept
    : pipeHandle { pipeHandle } {}

IpcWriter::~IpcWriter() noexcept = default;

void IpcWriter::SetHandle(const HANDLE handle) noexcept {
    std::lock_guard lock { mutex };
    pipeHandle = handle;
}

template <typename T>
StatusCode IpcWriter::SendPayload(
    const MessageType type, const T& payload) {
    std::lock_guard lock { mutex };
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

StatusCode IpcWriter::SendHeartbeat(
    const StatusHeartbeatMessage& msg) {
    return SendPayload(MessageType::StatusHeartbeat, msg);
}

StatusCode IpcWriter::SendHookStateChanged(
    const HookStateChangedMessage& msg) {
    return SendPayload(MessageType::HookStateChanged, msg);
}

StatusCode IpcWriter::SendError(const ErrorEventMessage& msg) {
    return SendPayload(MessageType::ErrorEvent, msg);
}

bool IpcWriter::IsConnected() const noexcept {
    return pipeHandle != INVALID_HANDLE_VALUE;
}

} // namespace z3lx::runtime
