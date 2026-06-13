#pragma once

#include "util/win/Loader.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>

#include <Windows.h>

namespace z3lx::util {
inline std::filesystem::path GetCurrentModuleFilePath() {
    const void* address = reinterpret_cast<void*>(GetCurrentModuleFilePath);
    return GetModuleFilePath(address);
}

template <typename Container>
void LoadRemoteLibrary(
    const HANDLE processHandle,
    const Container& libraryPaths) {
    if (libraryPaths.empty()) {
        return;
    }

    // Get LoadLibraryW
    const HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    THROW_LAST_ERROR_IF_NULL(kernel32);
    const FARPROC loadLibraryW = GetProcAddress(kernel32, "LoadLibraryW");
    THROW_LAST_ERROR_IF_NULL(loadLibraryW);

    // Calculate buffer size
    const std::filesystem::path& longestFilePath = *std::ranges::max_element(
        libraryPaths,
        [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return a.native().size() < b.native().size();
        }
    );
    const size_t bufferSize =
        (longestFilePath.native().size() + 1) * sizeof(wchar_t);

    // Allocate buffer
    const LPVOID buffer = VirtualAllocEx(
        processHandle,
        nullptr,
        bufferSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    THROW_LAST_ERROR_IF_NULL(buffer);
    const auto bufferCleanup = wil::scope_exit([=] {
        VirtualFreeEx(
            processHandle,
            buffer,
            0,
            MEM_RELEASE
        );
    });

    for (const std::filesystem::path& dllPath : libraryPaths) {
        // Write dll path to process
        THROW_IF_WIN32_BOOL_FALSE(WriteProcessMemory(
            processHandle,
            buffer,
            dllPath.c_str(),
            (dllPath.native().size() + 1) * sizeof(wchar_t),
            nullptr
        ));

        // Create thread to load dll
        const wil::unique_handle thread { CreateRemoteThread(
            processHandle,
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibraryW),
            buffer,
            0,
            nullptr
        ) };
        THROW_LAST_ERROR_IF_NULL(thread.get());
        WaitForSingleObject(thread.get(), INFINITE);
    }
}
} // namespace z3lx::util
