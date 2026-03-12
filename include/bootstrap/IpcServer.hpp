#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>
#include <string>

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
    void Close() noexcept;

private:
    uint32_t gamePid;
    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
    std::string pipeName;
};

} // namespace z3lx::bootstrap
