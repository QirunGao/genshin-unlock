#pragma once

#include "util/win/Loader.hpp"

#include <wil/result.h>

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
    const HANDLE threadHandle,
    const Container& libraryPaths) {
    if (libraryPaths.empty()) {
        return;
    }

    // No SeDebugPrivilege needed — the process handle from CreateProcessW
    // already has PROCESS_ALL_ACCESS for child processes we created.

    // Use LoadLibraryW directly from our IAT. kernel32.dll is mapped at
    // the same base address across all processes within a Windows session,
    // so the function pointer is valid in the target process as well.
    const auto loadLibrary = reinterpret_cast<PAPCFUNC>(&LoadLibraryW);

    for (const std::filesystem::path& dllPath : libraryPaths) {
        const size_t bufferSize =
            (dllPath.native().size() + 1) * sizeof(wchar_t);

        // Allocate a buffer in the target process for the DLL path
        const LPVOID buffer = VirtualAllocEx(
            processHandle,
            nullptr,
            bufferSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );
        THROW_LAST_ERROR_IF_NULL(buffer);

        // Write DLL path into the allocated buffer
        THROW_IF_WIN32_BOOL_FALSE(WriteProcessMemory(
            processHandle,
            buffer,
            dllPath.c_str(),
            bufferSize,
            nullptr
        ));

        // Queue an APC on the suspended main thread to call LoadLibraryW.
        // APCs queued on a CREATE_SUSPENDED thread execute in FIFO order
        // when the thread is resumed, before the process entry point runs.
        THROW_LAST_ERROR_IF(QueueUserAPC(
            loadLibrary,
            threadHandle,
            reinterpret_cast<ULONG_PTR>(buffer)
        ) == 0);
    }

    // Note: The allocated buffers are intentionally not freed here.
    // They are consumed by the queued APCs after ResumeThread is called.
    // The memory is reclaimed when the target process exits.
}
} // namespace z3lx::util
