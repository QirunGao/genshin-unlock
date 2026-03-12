#pragma once

#include "shared/StatusCode.hpp"

#include <cstdint>
#include <Windows.h>

namespace z3lx::shared {

// Named pipe naming convention: \\.\pipe\genshin-unlock-<gamePid>
constexpr uint32_t PROTOCOL_VERSION  = 1;
constexpr DWORD    PIPE_TIMEOUT_MS   = 15'000;
constexpr DWORD    PIPE_BUFFER_SIZE  = 4096;

enum class MessageType : uint32_t {
    Invalid          = 0,
    Hello            = 1,  // sent by launcher and bootstrap on connect
    BootstrapReady   = 2,  // bootstrap -> launcher: host validated
    RuntimeLoadRequest  = 3,  // launcher -> bootstrap: path to runtime.dll
    RuntimeInitResult   = 4,  // bootstrap -> launcher: init outcome
    ConfigSnapshot      = 5,  // launcher -> runtime: full config
    ConfigApplyResult   = 6,  // runtime -> launcher: config applied
    StatusHeartbeat     = 7,  // runtime -> launcher: periodic alive ping
    ErrorEvent          = 8,  // any -> launcher: error report
    ShutdownRequest     = 9,  // launcher -> runtime: ordered shutdown
};

// Every message on the pipe starts with this fixed-size header.
struct MessageHeader {
    MessageType type;
    uint32_t    payloadSize;
};

// ── Payloads ─────────────────────────────────────────────────────────────────

struct HelloPayload {
    uint32_t protocolVersion;
    uint32_t senderPid;
};

struct BootstrapReadyPayload {
    StatusCode validationStatus;
};

struct RuntimeLoadRequestPayload {
    wchar_t runtimePath[MAX_PATH];
};

struct RuntimeInitResultPayload {
    StatusCode status;
    bool       fpsAvailable;
    bool       fovAvailable;
};

// Fixed-size snapshot of RuntimeConfig transmitted over IPC.
// fovPresets is capped at 16 entries (far more than any realistic config).
constexpr int CONFIG_MAX_FOV_PRESETS = 16;

struct ConfigSnapshotPayload {
    bool     unlockFps;
    int      targetFps;
    bool     autoThrottle;

    bool     unlockFov;
    int      targetFov;
    int      fovPresets[CONFIG_MAX_FOV_PRESETS];
    int      fovPresetCount;
    float    fovSmoothing;

    uint8_t  unlockFovKey;
    uint8_t  nextFovPresetKey;
    uint8_t  prevFovPresetKey;

    // Game version forwarded by launcher so bootstrap can pass it to
    // RuntimeInitialize without the bootstrap needing to read files.
    uint16_t gameVersionMajor;
    uint16_t gameVersionMinor;
};

struct ConfigApplyResultPayload {
    StatusCode status;
};

struct StatusHeartbeatPayload {
    uint32_t   tickCount;
    StatusCode status;
    bool       fpsActive;
    bool       fovActive;
};

struct ErrorEventPayload {
    StatusCode status;
    char       module[64];
    char       phase[64];
    char       message[512];
};

// Convenience: build the well-known pipe name for a given game process ID.
// Returns a wstring of the form \\.\pipe\genshin-unlock-<pid>.
[[nodiscard]] std::wstring MakePipeName(DWORD gamePid);

// ── Runtime interface types ───────────────────────────────────────────────────
// Defined in shared so bootstrap.dll and runtime.dll agree on the ABI.

struct RuntimeInitParams {
    ConfigSnapshotPayload config;
    uint32_t              gameVersionMajor = 0;
    uint32_t              gameVersionMinor = 0;
};

struct RuntimeInitResult {
    StatusCode status       = StatusCode::Ok;
    bool       fpsAvailable = false;
    bool       fovAvailable = false;
};

} // namespace z3lx::shared
