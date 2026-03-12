#include "shared/VersionTable.hpp"

#include <algorithm>
#include <ranges>

namespace z3lx::shared {

VersionTable::VersionTable() noexcept = default;
VersionTable::~VersionTable() noexcept = default;

bool VersionTable::IsSupported(const util::Version& gameVersion) const noexcept {
    return std::ranges::any_of(entries,
        [&](const VersionEntry& e) {
            return e.gameVersion.GetMajor() == gameVersion.GetMajor() &&
                   e.gameVersion.GetMinor() == gameVersion.GetMinor();
        });
}

std::optional<ResolverProfile> VersionTable::GetProfile(
    const util::Version& gameVersion) const noexcept {
    auto it = std::ranges::find_if(entries,
        [&](const VersionEntry& e) {
            return e.gameVersion.GetMajor() == gameVersion.GetMajor() &&
                   e.gameVersion.GetMinor() == gameVersion.GetMinor();
        });
    if (it == entries.end()) {
        return std::nullopt;
    }

    auto profileIt = std::ranges::find_if(profiles,
        [&](const ResolverProfile& p) {
            return p.profileId == it->resolverProfileId;
        });
    if (profileIt == profiles.end()) {
        return std::nullopt;
    }

    return *profileIt;
}

void VersionTable::AddEntry(const VersionEntry& entry) {
    entries.push_back(entry);
}

void VersionTable::AddProfile(const ResolverProfile& profile) {
    profiles.push_back(profile);
}

} // namespace z3lx::shared
