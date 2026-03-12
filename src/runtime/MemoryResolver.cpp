#include "runtime/MemoryResolver.hpp"
#include "common/Constants.hpp"

#include <wil/result.h>

#include <Windows.h>

namespace z3lx::runtime {

MemoryResolver::MemoryResolver() noexcept = default;
MemoryResolver::~MemoryResolver() noexcept = default;

StatusCode MemoryResolver::Resolve(
    const VersionTable& table,
    const util::Version& gameVersion) {
    // Determine game module and region
    if (const HMODULE osModule = GetModuleHandleW(common::osGameFileName);
        osModule != nullptr) {
        gameModule = osModule;
        region = GameRegion::OS;
    } else if (const HMODULE cnModule = GetModuleHandleW(common::cnGameFileName);
        cnModule != nullptr) {
        gameModule = cnModule;
        region = GameRegion::CN;
    } else {
        return StatusCode::SymbolResolveFailed;
    }

    // Get resolver profile for this game version
    const auto profile = table.GetProfile(gameVersion);
    if (!profile) {
        return StatusCode::GameVersionUnsupported;
    }

    return ResolveByOffsetTable(*profile);
}

StatusCode MemoryResolver::ResolveByOffsetTable(const ResolverProfile& profile) {
    const auto& offsets = (region == GameRegion::OS)
        ? profile.osOffsets
        : profile.cnOffsets;

    const auto baseAddress = reinterpret_cast<uintptr_t>(gameModule);

    ResolvedAddresses resolved {};
    resolved.region = region;

    if (offsets.fpsOffset != 0) {
        resolved.fpsAddress = reinterpret_cast<int*>(
            baseAddress + offsets.fpsOffset);
    }
    if (offsets.fovOffset != 0) {
        resolved.fovTarget = reinterpret_cast<void*>(
            baseAddress + offsets.fovOffset);
    }

    addresses = resolved;
    return StatusCode::Ok;
}

std::optional<ResolvedAddresses> MemoryResolver::GetAddresses() const noexcept {
    return addresses;
}

bool MemoryResolver::IsFpsResolved() const noexcept {
    return addresses.has_value() && addresses->fpsAddress != nullptr;
}

bool MemoryResolver::IsFovResolved() const noexcept {
    return addresses.has_value() && addresses->fovTarget != nullptr;
}

} // namespace z3lx::runtime
