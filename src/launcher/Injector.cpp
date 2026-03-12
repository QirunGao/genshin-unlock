#include "launcher/Injector.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <cstdint>
#include <filesystem>

#include <Windows.h>

namespace z3lx::launcher {

StatusCode InjectBootstrap(
    const HANDLE processHandle,
    const std::filesystem::path& bootstrapPath) {
    // Reject relative paths and network paths
    if (bootstrapPath.is_relative()) {
        return StatusCode::ModuleSignatureInvalid;
    }
    const std::wstring pathStr = bootstrapPath.native();
    if (pathStr.starts_with(L"\\\\")) {
        return StatusCode::ModuleSignatureInvalid;
    }

    const size_t bufferSize =
        (pathStr.size() + 1) * sizeof(wchar_t);

    // Allocate buffer in target process for the DLL path
    const LPVOID buffer = VirtualAllocEx(
        processHandle,
        nullptr,
        bufferSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!buffer) {
        return StatusCode::InjectFailed;
    }

    // Write DLL path into allocated buffer
    if (!WriteProcessMemory(
        processHandle,
        buffer,
        pathStr.c_str(),
        bufferSize,
        nullptr)) {
        VirtualFreeEx(processHandle, buffer, 0, MEM_RELEASE);
        return StatusCode::InjectFailed;
    }

    // CreateRemoteThread to call LoadLibraryW
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")
    );
    if (!loadLibrary) {
        VirtualFreeEx(processHandle, buffer, 0, MEM_RELEASE);
        return StatusCode::InjectFailed;
    }

    wil::unique_handle remoteThread {
        CreateRemoteThread(
            processHandle,
            nullptr,
            0,
            loadLibrary,
            buffer,
            0,
            nullptr)
    };
    if (!remoteThread) {
        VirtualFreeEx(processHandle, buffer, 0, MEM_RELEASE);
        return StatusCode::InjectFailed;
    }

    // Wait for the remote thread to complete
    const DWORD waitResult = WaitForSingleObject(remoteThread.get(), 10000);
    if (waitResult != WAIT_OBJECT_0) {
        VirtualFreeEx(processHandle, buffer, 0, MEM_RELEASE);
        return StatusCode::InjectFailed;
    }

    // Check the exit code (the return value of LoadLibraryW)
    DWORD exitCode = 0;
    GetExitCodeThread(remoteThread.get(), &exitCode);
    if (exitCode == 0) {
        VirtualFreeEx(processHandle, buffer, 0, MEM_RELEASE);
        return StatusCode::InjectFailed;
    }

    // Clean up the allocated buffer
    VirtualFreeEx(processHandle, buffer, 0, MEM_RELEASE);

    return StatusCode::Ok;
}

} // namespace z3lx::launcher
