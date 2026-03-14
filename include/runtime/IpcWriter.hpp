#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>
#include <memory>
#include <mutex>

#include <Windows.h>

namespace z3lx::runtime {

using namespace z3lx::shared;

/// Lightweight IPC message writer used by the runtime to send
/// heartbeat, hook-state, and error events back to the launcher.
class IpcWriter {
public:
    IpcWriter() noexcept;
    explicit IpcWriter(HANDLE pipeHandle) noexcept;
    ~IpcWriter() noexcept;

    IpcWriter(IpcWriter&& other) noexcept;
    IpcWriter& operator=(IpcWriter&& other) noexcept;
    IpcWriter(const IpcWriter&) = delete;
    IpcWriter& operator=(const IpcWriter&) = delete;

    void SetHandle(HANDLE pipeHandle) noexcept;

    StatusCode SendConfigApplyResult(const ConfigApplyResultMessage& msg);
    StatusCode SendControlPlaneReady(const ControlPlaneReadyMessage& msg);
    StatusCode SendStateChanged(const StateChangedMessage& msg);
    StatusCode SendHeartbeat(const StatusHeartbeatMessage& msg);
    StatusCode SendHookStateChanged(const HookStateChangedMessage& msg);
    StatusCode SendLogEvent(const LogEventMessage& msg);
    StatusCode SendError(const ErrorEventMessage& msg);

    bool HasPendingData() const noexcept;
    StatusCode PeekMessageHeader(MessageHeader& header);
    StatusCode ReceiveShutdown(ShutdownRequestMessage& msg);
    [[nodiscard]] bool IsConnected() const noexcept;

private:
    template <typename T>
    StatusCode SendPayload(MessageType type, const T& payload);

    template <typename T>
    StatusCode ReceivePayload(MessageType expectedType, T& payload);

    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
    mutable std::unique_ptr<std::mutex> mutex;
};

} // namespace z3lx::runtime
