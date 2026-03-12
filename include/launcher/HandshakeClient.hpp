#pragma once

#include "shared/ConfigModel.hpp"
#include "shared/Protocol.hpp"

#include <filesystem>
#include <Windows.h>

namespace z3lx::launcher {

// Named-pipe client for the launcher ↔ bootstrap/runtime handshake.
//
// Lifecycle:
//   1. Construct with the game process ID (creates the pipe server).
//   2. Call WaitForBootstrap() after injection.
//   3. Call SendRuntimePath() + SendConfigSnapshot().
//   4. Call ReceiveInitResult().
//   5. Resume the game's main thread.
//   6. Optionally call MonitorRuntime() for heartbeat/error logging.
class HandshakeClient {
public:
    explicit HandshakeClient(DWORD gamePid);
    ~HandshakeClient() noexcept;

    HandshakeClient(const HandshakeClient&)            = delete;
    HandshakeClient& operator=(const HandshakeClient&) = delete;

    // Blocks until bootstrap connects and sends Hello + BootstrapReady.
    // timeoutMs applies to the ConnectNamedPipe wait.
    void WaitForBootstrap(DWORD timeoutMs = shared::PIPE_TIMEOUT_MS);

    // Sends RuntimeLoadRequest containing the absolute path to runtime.dll.
    void SendRuntimePath(const std::filesystem::path& runtimePath);

    // Sends ConfigSnapshot derived from the given RuntimeConfig.
    // gameVersionMajor/Minor are forwarded so the bootstrap can pass them
    // to RuntimeInitialize without needing to read config.ini itself.
    void SendConfigSnapshot(const shared::RuntimeConfig& config,
                            uint16_t gameVersionMajor,
                            uint16_t gameVersionMinor);

    // Blocks until RuntimeInitResult is received from the runtime.
    [[nodiscard]] shared::RuntimeInitResultPayload ReceiveInitResult(
        DWORD timeoutMs = shared::PIPE_TIMEOUT_MS);

    // Reads incoming messages (heartbeat / error) until the pipe disconnects.
    // Called after the game main thread is resumed.
    void MonitorRuntime();

private:
    void Write(const void* data, DWORD bytes);
    void Read(void* data, DWORD bytes);
    void SendMessage(shared::MessageType type,
                     const void* payload, DWORD payloadSize);
    void RecvMessage(shared::MessageType expectedType,
                     void* payload, DWORD payloadSize,
                     DWORD timeoutMs = INFINITE);

    HANDLE pipeHandle_ = INVALID_HANDLE_VALUE;
};

} // namespace z3lx::launcher
