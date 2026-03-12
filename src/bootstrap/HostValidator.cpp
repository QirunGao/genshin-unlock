#include "bootstrap/HostValidator.hpp"
#include "common/Constants.hpp"

#include <wil/result.h>

#include <string>

#include <Windows.h>

namespace z3lx::bootstrap {

HostValidationResult ValidateHost() {
    HostValidationResult result {};

    // Check if host is one of the known game executables
    if (const HMODULE osModule = GetModuleHandleW(common::osGameFileName);
        osModule != nullptr) {
        result.hostModuleName = "GenshinImpact.exe";
        result.status = StatusCode::Ok;
        return result;
    }
    if (const HMODULE cnModule = GetModuleHandleW(common::cnGameFileName);
        cnModule != nullptr) {
        result.hostModuleName = "YuanShen.exe";
        result.status = StatusCode::Ok;
        return result;
    }

    result.status = StatusCode::BootstrapInitFailed;
    return result;
}

} // namespace z3lx::bootstrap
