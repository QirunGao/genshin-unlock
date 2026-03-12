#pragma once

#include "StatusCode.hpp"

#include <cstdint>
#include <string>

namespace z3lx::shared {

constexpr uint32_t kProtocolVersion = 1;
constexpr auto kPipeNamePrefix = R"(\\.\pipe\z3lx-genshin-unlock-)";

enum class MessageType : uint32_t {
    Hello = 1,
    BootstrapReady,
    RuntimeLoadRequest,
    RuntimeInitResult,
    ConfigSnapshot,
    ConfigApplyResult,
    StatusHeartbeat,
    HookStateChanged,
    ErrorEvent,
    ShutdownRequest
};

struct MessageHeader {
    MessageType type;
    uint32_t payloadSize;
};

struct IpcSession {
    uint32_t sessionId = 0;
    uint32_t launcherPid = 0;
    uint32_t gamePid = 0;
    std::string toolVersion;
    std::string gameVersion;
    uint32_t protocolVersion = kProtocolVersion;
};

struct HelloMessage {
    IpcSession session;
};

struct BootstrapReadyMessage {
    StatusCode status = StatusCode::Ok;
    std::string hostModuleName;
    std::string hostVersion;
};

struct RuntimeLoadRequestMessage {
    std::string runtimePath;
};

struct RuntimeInitResultMessage {
    StatusCode status = StatusCode::Ok;
    bool fpsAvailable = false;
    bool fovAvailable = false;
    std::string details;
};

struct ConfigSnapshotMessage {
    uint32_t version = 0;
    bool unlockFps = true;
    int targetFps = 120;
    bool autoThrottle = true;
    bool unlockFov = true;
    int targetFov = 90;
    float fovSmoothing = 0.125f;
};

struct ConfigApplyResultMessage {
    StatusCode status = StatusCode::Ok;
    uint32_t configVersion = 0;
};

struct StatusHeartbeatMessage {
    uint32_t runtimeState = 0;
    bool fpsActive = false;
    bool fovActive = false;
    uint32_t uptimeSeconds = 0;
};

struct HookStateChangedMessage {
    std::string hookName;
    bool installed = false;
    bool enabled = false;
    StatusCode status = StatusCode::Ok;
};

struct ErrorEventMessage {
    ErrorDetail error;
};

struct ShutdownRequestMessage {
    bool graceful = true;
};

} // namespace z3lx::shared
