#pragma once

#include "shared/StatusCode.hpp"

#include <filesystem>
#include <string>

namespace z3lx::bootstrap {

using namespace z3lx::shared;

struct HostValidationResult {
    StatusCode status = StatusCode::Ok;
    uint32_t systemError = 0;
    std::string hostModuleName;
    std::string hostVersion;
    std::string phaseName = "host_validation";
    std::string message;
    std::filesystem::path hostModulePath;
};

HostValidationResult ValidateHost();

} // namespace z3lx::bootstrap
