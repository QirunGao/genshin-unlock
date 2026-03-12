#pragma once

#include "util/Version.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace z3lx::shared {

struct OffsetEntry {
    uintptr_t fpsOffset = 0;
    uintptr_t fovOffset = 0;
};

struct ResolverProfile {
    std::string profileId;
    OffsetEntry osOffsets;
    OffsetEntry cnOffsets;
};

struct VersionEntry {
    util::Version gameVersion;
    std::string resolverProfileId;
};

class VersionTable {
public:
    VersionTable() noexcept;
    ~VersionTable() noexcept;

    [[nodiscard]] bool IsSupported(const util::Version& gameVersion) const noexcept;

    [[nodiscard]] std::optional<ResolverProfile> GetProfile(
        const util::Version& gameVersion) const noexcept;

    void AddEntry(const VersionEntry& entry);
    void AddProfile(const ResolverProfile& profile);

private:
    std::vector<VersionEntry> entries;
    std::vector<ResolverProfile> profiles;
};

inline VersionTable MakeDefaultVersionTable() {
    VersionTable table;

    // Resolver profile A: version 5.4.x
    table.AddProfile(ResolverProfile {
        .profileId = "profile_a",
        .osOffsets = { .fpsOffset = 0x49965C4, .fovOffset = 0x1091A20 },
        .cnOffsets = { .fpsOffset = 0x49965C4, .fovOffset = 0x1092A20 }
    });

    // Map known game versions to profiles
    table.AddEntry(VersionEntry {
        .gameVersion = util::Version { 5, 4, 0, 0 },
        .resolverProfileId = "profile_a"
    });

    return table;
}

} // namespace z3lx::shared
