#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace z3lx::shared {

enum class StatusCode : uint32_t {
    Ok = 0,
    ConfigInvalid,
    ProtocolMismatch,
    GameNotFound,
    GameVersionUnsupported,
    ModuleSignatureInvalid,
    InjectFailed,
    BootstrapInitFailed,
    RuntimeLoadFailed,
    RuntimeInitFailed,
    SymbolResolveFailed,
    HookInstallFailed,
    IpcDisconnected
};

struct ErrorDetail {
    std::string moduleName;
    std::string phaseName;
    StatusCode code = StatusCode::Ok;
    uint32_t systemError = 0;
    std::string message;
};

constexpr std::string_view StatusCodeToString(const StatusCode code) noexcept {
    switch (code) {
    case StatusCode::Ok:                       return "Ok";
    case StatusCode::ConfigInvalid:            return "ConfigInvalid";
    case StatusCode::ProtocolMismatch:         return "ProtocolMismatch";
    case StatusCode::GameNotFound:             return "GameNotFound";
    case StatusCode::GameVersionUnsupported:   return "GameVersionUnsupported";
    case StatusCode::ModuleSignatureInvalid:   return "ModuleSignatureInvalid";
    case StatusCode::InjectFailed:             return "InjectFailed";
    case StatusCode::BootstrapInitFailed:      return "BootstrapInitFailed";
    case StatusCode::RuntimeLoadFailed:        return "RuntimeLoadFailed";
    case StatusCode::RuntimeInitFailed:        return "RuntimeInitFailed";
    case StatusCode::SymbolResolveFailed:      return "SymbolResolveFailed";
    case StatusCode::HookInstallFailed:        return "HookInstallFailed";
    case StatusCode::IpcDisconnected:          return "IpcDisconnected";
    default:                                   return "Unknown";
    }
}

} // namespace z3lx::shared
