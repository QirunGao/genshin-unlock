#pragma once

#include <cstdint>

namespace z3lx::shared {

enum class StatusCode : uint32_t {
    Ok = 0,
    ConfigInvalid,
    GameNotFound,
    GameVersionUnsupported,
    ModuleSignatureInvalid,
    InjectFailed,
    BootstrapInitFailed,
    RuntimeLoadFailed,
    RuntimeInitFailed,
    SymbolResolveFailed,
    HookInstallFailed,
    IpcDisconnected,
};

[[nodiscard]] const char* StatusCodeToString(StatusCode code) noexcept;

} // namespace z3lx::shared
