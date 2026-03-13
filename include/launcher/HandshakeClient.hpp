#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>

#include <Windows.h>

namespace z3lx::launcher {

using namespace z3lx::shared;

class HandshakeClient {
public:
    explicit HandshakeClient(uint32_t gamePid) noexcept;
    ~HandshakeClient() noexcept;

    StatusCode Connect(uint32_t timeoutMs = 10000);
    StatusCode SendHello(const HelloMessage& msg);
    StatusCode WaitForBootstrapReady(BootstrapReadyMessage& response, uint32_t timeoutMs = 10000);
    StatusCode SendConfigSnapshot(const ConfigSnapshotMessage& config);
    StatusCode WaitForRuntimeInitResult(RuntimeInitResultMessage& response, uint32_t timeoutMs = 10000);
    StatusCode SendShutdown(const ShutdownRequestMessage& msg);

    // Runtime event receiving (monitoring phase)
    bool HasPendingData() const noexcept;
    StatusCode PeekMessageHeader(MessageHeader& header);
    StatusCode ReceiveHeartbeat(StatusHeartbeatMessage& msg);
    StatusCode ReceiveHookStateChanged(HookStateChangedMessage& msg);
    StatusCode ReceiveError(ErrorEventMessage& msg);

    [[nodiscard]] bool IsConnected() const noexcept;
    void Disconnect() noexcept;

private:
    template <typename T>
    StatusCode SendPayload(MessageType type, const T& payload);

    template <typename T>
    StatusCode ReceivePayload(MessageType expectedType, T& payload);

    uint32_t gamePid;
    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
};

} // namespace z3lx::launcher
