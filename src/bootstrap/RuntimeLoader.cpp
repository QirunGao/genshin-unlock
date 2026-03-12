#include "bootstrap/RuntimeLoader.hpp"

#include <filesystem>

#include <Windows.h>

namespace z3lx::bootstrap {

RuntimeLoadResult LoadRuntime(const std::filesystem::path& runtimePath) {
    RuntimeLoadResult result {};

    // Reject relative paths
    if (runtimePath.is_relative()) {
        result.status = StatusCode::ModuleSignatureInvalid;
        return result;
    }

    // Reject network paths
    const std::wstring pathStr = runtimePath.native();
    if (pathStr.starts_with(L"\\\\")) {
        result.status = StatusCode::ModuleSignatureInvalid;
        return result;
    }

    // Verify file exists
    if (!std::filesystem::exists(runtimePath)) {
        result.status = StatusCode::RuntimeLoadFailed;
        return result;
    }

    // Load the module
    const HMODULE module = LoadLibraryW(runtimePath.c_str());
    if (!module) {
        result.status = StatusCode::RuntimeLoadFailed;
        return result;
    }

    result.runtimeModule = module;
    result.status = StatusCode::Ok;
    return result;
}

void UnloadRuntime(const HMODULE runtimeModule) {
    if (runtimeModule) {
        FreeLibrary(runtimeModule);
    }
}

} // namespace z3lx::bootstrap
