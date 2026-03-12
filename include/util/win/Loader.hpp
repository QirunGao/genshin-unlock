#pragma once

#include <filesystem>
#include <string_view>

#include <Windows.h>

namespace z3lx::util {
std::filesystem::path GetCurrentModuleFilePath();

std::filesystem::path GetModuleFilePath(HMODULE module);
std::filesystem::path GetModuleFilePath(const void* address);
std::filesystem::path GetModuleFilePath(std::string_view moduleName);
std::filesystem::path GetModuleFilePath(std::wstring_view moduleName);

void LoadRemoteLibrary(
    HANDLE processHandle,
    HANDLE threadHandle,
    const std::filesystem::path& libraryPath
);
template <typename Container>
void LoadRemoteLibrary(
    HANDLE processHandle,
    HANDLE threadHandle,
    const Container& libraryPaths
);
} // namespace z3lx::util

#include "util/win/LoaderInl.hpp"
