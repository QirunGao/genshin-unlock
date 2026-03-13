#pragma once

#include "shared/StatusCode.hpp"

#include <filesystem>
#include <string>

namespace z3lx::launcher {

using namespace z3lx::shared;

/// Compute the SHA-256 hash of a file and return it as a lowercase
/// hex string.  Returns an empty string on failure.
std::string ComputeFileSha256(const std::filesystem::path& filePath);

/// Validate a module file's integrity by computing its SHA-256 hash
/// and comparing it against the expected value.
/// Returns StatusCode::Ok on match, StatusCode::ModuleSignatureInvalid
/// on mismatch, or an appropriate error on I/O failure.
StatusCode ValidateModuleHash(
    const std::filesystem::path& modulePath,
    const std::string& expectedSha256Hex);

} // namespace z3lx::launcher
