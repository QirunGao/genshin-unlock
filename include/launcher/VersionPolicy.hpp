#pragma once

#include "shared/VersionTable.hpp"
#include "util/Version.hpp"

#include <filesystem>

namespace z3lx::launcher {

// Parses the game version from the config.ini file alongside the game exe.
// Throws when the file is missing or the version key is absent.
[[nodiscard]] util::Version GetGameVersion(
    const std::filesystem::path& gamePath);

// Checks game version against the whitelist.
// Returns the matching VersionEntry on success.
// Throws std::runtime_error with StatusCode::GameVersionUnsupported on failure.
[[nodiscard]] shared::VersionEntry CheckVersionCompatibility(
    const util::Version& modVersion,
    const util::Version& gameVersion);

} // namespace z3lx::launcher
