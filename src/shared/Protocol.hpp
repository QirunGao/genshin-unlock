#pragma once

#include "StatusCode.hpp"

#include <cstdint>
#include <cstring>

namespace z3lx::shared {

constexpr uint32_t kProtocolVersion = 1;
constexpr auto kPipeNamePrefix = R"(\\.\pipe\z3lx-genshin-unlock-)";

constexpr size_t kMaxStringLen = 64;
constexpr size_t kMaxFovPresets = 16;

// Helper to safely copy a C-string into a fixed-size char buffer
inline void CopyToFixedString(
    char* dest, const size_t destSize, const char* src) noexcept {
    if (!src || destSize == 0) {
        if (destSize > 0) dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

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

// All message structs are POD (trivially copyable) so they can be
// sent/received over named pipes without serialization.

struct IpcSession {
    uint32_t sessionId = 0;
    uint32_t launcherPid = 0;
    uint32_t gamePid = 0;
    char toolVersion[kMaxStringLen] = {};
    char gameVersion[kMaxStringLen] = {};
    uint32_t protocolVersion = kProtocolVersion;
};

struct HelloMessage {
    IpcSession session;
};

struct BootstrapReadyMessage {
    StatusCode status = StatusCode::Ok;
    char hostModuleName[kMaxStringLen] = {};
    char hostVersion[kMaxStringLen] = {};
};

struct RuntimeLoadRequestMessage {
    char runtimePath[260] = {};
};

struct RuntimeInitResultMessage {
    StatusCode status = StatusCode::Ok;
    uint32_t fpsAvailable = 0;
    uint32_t fovAvailable = 0;
};

struct ConfigSnapshotMessage {
    uint32_t version = 0;
    uint32_t unlockFps = 1;
    int32_t targetFps = 120;
    uint32_t autoThrottle = 1;
    uint32_t unlockFov = 1;
    int32_t targetFov = 90;
    float fovSmoothing = 0.125f;
    uint8_t unlockFovKey = 0;
    uint8_t nextFovPresetKey = 0;
    uint8_t prevFovPresetKey = 0;
    uint8_t _pad0 = 0;
    int32_t fovPresets[kMaxFovPresets] = {};
    uint32_t fovPresetCount = 0;
};

struct ConfigApplyResultMessage {
    StatusCode status = StatusCode::Ok;
    uint32_t configVersion = 0;
};

struct StatusHeartbeatMessage {
    uint32_t runtimeState = 0;
    uint32_t fpsActive = 0;
    uint32_t fovActive = 0;
    uint32_t uptimeSeconds = 0;
};

struct HookStateChangedMessage {
    char hookName[kMaxStringLen] = {};
    uint32_t installed = 0;
    uint32_t enabled = 0;
    StatusCode status = StatusCode::Ok;
};

struct ErrorEventMessage {
    StatusCode code = StatusCode::Ok;
    uint32_t systemError = 0;
    char moduleName[kMaxStringLen] = {};
    char message[kMaxStringLen] = {};
};

struct ShutdownRequestMessage {
    uint32_t graceful = 1;
};

} // namespace z3lx::shared
