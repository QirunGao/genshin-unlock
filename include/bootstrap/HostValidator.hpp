#pragma once

#include "shared/StatusCode.hpp"

#include <string>

namespace z3lx::bootstrap {

using namespace z3lx::shared;

struct HostValidationResult {
    StatusCode status = StatusCode::Ok;
    std::string hostModuleName;
    std::string hostVersion;
};

HostValidationResult ValidateHost();

} // namespace z3lx::bootstrap
