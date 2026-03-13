#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>

#include <Windows.h>

namespace z3lx::bootstrap {

using namespace z3lx::shared;

class IpcServer {
public:
    explicit IpcServer(uint32_t gamePid) noexcept;
    ~IpcServer() noexcept;

    StatusCode Create();
    StatusCode WaitForConnection(uint32_t timeoutMs = 10000);
    StatusCode ReceiveHello(HelloMessage& msg);
    StatusCode SendBootstrapReady(const BootstrapReadyMessage& msg);
    StatusCode ReceiveConfigSnapshot(ConfigSnapshotMessage& config);
    StatusCode SendRuntimeInitResult(const RuntimeInitResultMessage& result);
    StatusCode SendHeartbeat(const StatusHeartbeatMessage& heartbeat);
    StatusCode SendHookStateChanged(const HookStateChangedMessage& hookState);
    StatusCode SendError(const ErrorEventMessage& error);

    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] HANDLE ReleaseHandle() noexcept;
    void Close() noexcept;

private:
    template <typename T>
    StatusCode SendPayload(MessageType type, const T& payload);

    template <typename T>
    StatusCode ReceivePayload(MessageType expectedType, T& payload);

    uint32_t gamePid;
    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
};

} // namespace z3lx::bootstrap
