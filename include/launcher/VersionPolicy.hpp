#pragma once

#include "shared/VersionTable.hpp"
#include "util/Version.hpp"

#include <filesystem>

namespace z3lx::launcher {

using namespace z3lx::shared;

util::Version ReadGameVersion(const std::filesystem::path& gamePath);

bool IsVersionSupported(
    const util::Version& gameVersion,
    const VersionTable& table);

void CheckVersionCompatibility(
    const util::Version& toolVersion,
    const util::Version& gameVersion,
    const VersionTable& table);

} // namespace z3lx::launcher
