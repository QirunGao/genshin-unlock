#include "shared/StatusCode.hpp"
#include "shared/VersionTable.hpp"
#include "shared/Protocol.hpp"

#include <array>
#include <format>

namespace z3lx::shared {

// ── Status code strings ───────────────────────────────────────────────────────

const char* StatusCodeToString(const StatusCode code) noexcept {
    switch (code) {
    case StatusCode::Ok:                      return "Ok";
    case StatusCode::ConfigInvalid:           return "ConfigInvalid";
    case StatusCode::GameNotFound:            return "GameNotFound";
    case StatusCode::GameVersionUnsupported:  return "GameVersionUnsupported";
    case StatusCode::ModuleSignatureInvalid:  return "ModuleSignatureInvalid";
    case StatusCode::InjectFailed:            return "InjectFailed";
    case StatusCode::BootstrapInitFailed:     return "BootstrapInitFailed";
    case StatusCode::RuntimeLoadFailed:       return "RuntimeLoadFailed";
    case StatusCode::RuntimeInitFailed:       return "RuntimeInitFailed";
    case StatusCode::SymbolResolveFailed:     return "SymbolResolveFailed";
    case StatusCode::HookInstallFailed:       return "HookInstallFailed";
    case StatusCode::IpcDisconnected:         return "IpcDisconnected";
    default:                                  return "Unknown";
    }
}

// ── Version whitelist ─────────────────────────────────────────────────────────

namespace {

constexpr std::array<VersionEntry, 2> VERSION_TABLE {{
    { 6, 4, ResolverProfile::ProfileA },
    { 6, 5, ResolverProfile::ProfileA },
}};

constexpr std::array<std::pair<ResolverProfile, ResolverAddresses>, 1>
RESOLVER_TABLE {{
    {
        ResolverProfile::ProfileA,
        {
            .fpsOffset   = 0x49965C4,
            .fovOffsetOS = 0x1091A20,
            .fovOffsetCN = 0x1092A20,
        }
    },
}};

} // namespace

std::optional<VersionEntry> FindVersionEntry(
    const uint16_t major, const uint16_t minor) noexcept
{
    for (const auto& entry : VERSION_TABLE) {
        if (entry.major == major && entry.minor == minor) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<ResolverAddresses> GetResolverAddresses(
    const ResolverProfile profile) noexcept
{
    for (const auto& [p, addrs] : RESOLVER_TABLE) {
        if (p == profile) {
            return addrs;
        }
    }
    return std::nullopt;
}

// ── Pipe name helper ──────────────────────────────────────────────────────────

std::wstring MakePipeName(const DWORD gamePid) {
    return std::format(L"\\\\.\\pipe\\genshin-unlock-{}", gamePid);
}

} // namespace z3lx::shared
