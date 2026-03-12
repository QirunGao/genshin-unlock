#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>
#include <string>

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

    [[nodiscard]] bool IsConnected() const noexcept;
    void Disconnect() noexcept;

private:
    uint32_t gamePid;
    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
    std::string pipeName;
};

} // namespace z3lx::launcher
