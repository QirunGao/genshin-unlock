#pragma once

#include <cstdint>
#include <optional>

namespace z3lx::shared {

// Resolver profile identifies the memory layout for a range of game versions.
enum class ResolverProfile : uint8_t {
    None    = 0,
    ProfileA,  // 6.4.x – 6.5.x
};

struct VersionEntry {
    uint16_t       major;
    uint16_t       minor;
    ResolverProfile profile;
};

// Absolute offsets from the game module base address.
struct ResolverAddresses {
    uintptr_t fpsOffset;    // target-FPS variable
    uintptr_t fovOffsetOS;  // SetFieldOfView function – OS (Global) build
    uintptr_t fovOffsetCN;  // SetFieldOfView function – CN build
};

// Returns the whitelist entry for a given game major.minor, or nullopt when
// the version is unknown / unsupported (default-deny).
[[nodiscard]] std::optional<VersionEntry> FindVersionEntry(
    uint16_t major, uint16_t minor) noexcept;

// Returns the resolver addresses for the given profile, or nullopt when the
// profile has no registered address map (should not normally happen).
[[nodiscard]] std::optional<ResolverAddresses> GetResolverAddresses(
    ResolverProfile profile) noexcept;

} // namespace z3lx::shared
