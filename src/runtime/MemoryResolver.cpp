#include "runtime/MemoryResolver.hpp"
#include "shared/VersionTable.hpp"
#include "common/Constants.hpp"

#include <Windows.h>

namespace z3lx::runtime {

std::optional<MemoryResolver::Addresses> MemoryResolver::Resolve(
    const uint16_t gameVersionMajor,
    const uint16_t gameVersionMinor)
{
    const auto entry =
        z3lx::shared::FindVersionEntry(gameVersionMajor, gameVersionMinor);
    if (!entry) {
        return std::nullopt;
    }

    const auto addrs = z3lx::shared::GetResolverAddresses(entry->profile);
    if (!addrs) {
        return std::nullopt;
    }

    // Determine which game module is loaded (OS or CN build)
    const HMODULE osModule = GetModuleHandleW(z3lx::common::osGameFileName);
    const HMODULE cnModule = GetModuleHandleW(z3lx::common::cnGameFileName);
    const HMODULE gameModule = osModule ? osModule : cnModule;
    if (!gameModule) {
        return std::nullopt;
    }

    const auto base = reinterpret_cast<uintptr_t>(gameModule);
    const uintptr_t fovOffset = osModule
        ? addrs->fovOffsetOS
        : addrs->fovOffsetCN;

    return Addresses {
        .fpsTarget = reinterpret_cast<int*>(base + addrs->fpsOffset),
        .fovFunc   = reinterpret_cast<void*>(base + fovOffset),
    };
}

} // namespace z3lx::runtime
