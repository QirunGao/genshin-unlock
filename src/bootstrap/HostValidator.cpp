#include "bootstrap/HostValidator.hpp"
#include "common/Constants.hpp"
#include "util/win/Version.hpp"

#include <wil/result.h>

#include <filesystem>
#include <string>

#include <Windows.h>

namespace z3lx::bootstrap {

HostValidationResult ValidateHost() {
    HostValidationResult result {};

    const auto validateModule = [&](const HMODULE module,
                                    const char* moduleName) -> bool {
        if (!module) {
            return false;
        }

        wchar_t modulePathBuffer[MAX_PATH] {};
        const DWORD pathLen = GetModuleFileNameW(
            module, modulePathBuffer, MAX_PATH);
        if (pathLen == 0 || pathLen >= MAX_PATH) {
            result.status = StatusCode::BootstrapInitFailed;
            result.systemError = GetLastError();
            result.message = "Failed to resolve the host module path.";
            return true;
        }

        const std::filesystem::path modulePath { modulePathBuffer };
        const std::wstring nativePath = modulePath.native();
        if (modulePath.is_relative() || nativePath.starts_with(L"\\\\")) {
            result.status = StatusCode::BootstrapInitFailed;
            result.message = "Host module path must be absolute and local.";
            return true;
        }

        try {
            result.hostVersion = util::GetFileVersion(modulePath).ToString();
        } catch (...) {
            result.status = StatusCode::BootstrapInitFailed;
            result.message = "Failed to read the host module version.";
            return true;
        }

        result.hostModuleName = moduleName;
        result.hostModulePath = modulePath;
        result.message = "Host module validated successfully.";
        result.status = StatusCode::Ok;
        return true;
    };

    if (validateModule(GetModuleHandleW(common::osGameFileName),
            "GenshinImpact.exe")) {
        return result;
    }
    if (validateModule(GetModuleHandleW(common::cnGameFileName),
            "YuanShen.exe")) {
        return result;
    }

    result.status = StatusCode::BootstrapInitFailed;
    result.message = "Bootstrap was not loaded inside a supported game process.";
    return result;
}

} // namespace z3lx::bootstrap
