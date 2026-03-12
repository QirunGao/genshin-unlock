#include "launcher/Injector.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <cstdint>
#include <filesystem>

#include <Windows.h>
#include <TlHelp32.h>

namespace z3lx::launcher {

namespace {

HMODULE FindRemoteModule(
    const uint32_t processId, const wchar_t* moduleName) {
    const wil::unique_handle snapshot {
        CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId)
    };
    if (!snapshot) return nullptr;

    MODULEENTRY32W entry { .dwSize = sizeof(entry) };
    if (!Module32FirstW(snapshot.get(), &entry)) return nullptr;

    do {
        if (_wcsicmp(entry.szModule, moduleName) == 0) {
            return reinterpret_cast<HMODULE>(entry.modBaseAddr);
        }
    } while (Module32NextW(snapshot.get(), &entry));

    return nullptr;
}

} // namespace

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

StatusCode CallBootstrapEntry(
    const HANDLE processHandle,
    const uint32_t processId,
    const std::filesystem::path& bootstrapPath) {
    // Step 1: Find bootstrap.dll base in remote process
    const HMODULE remoteBase = FindRemoteModule(
        processId, bootstrapPath.filename().c_str());
    if (!remoteBase) {
        return StatusCode::BootstrapInitFailed;
    }

    // Step 2: Load bootstrap.dll locally to compute function offset
    const HMODULE localModule = LoadLibraryW(bootstrapPath.c_str());
    if (!localModule) {
        return StatusCode::BootstrapInitFailed;
    }

    const auto localFunc = reinterpret_cast<uintptr_t>(
        GetProcAddress(localModule, "BootstrapEntryPoint"));
    const auto localBase = reinterpret_cast<uintptr_t>(localModule);
    FreeLibrary(localModule);

    if (!localFunc) {
        return StatusCode::BootstrapInitFailed;
    }

    // Step 3: Calculate remote function address
    const uintptr_t offset = localFunc - localBase;
    const auto remoteFunc = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<uintptr_t>(remoteBase) + offset);

    // Step 4: Create remote thread (non-blocking)
    wil::unique_handle remoteThread {
        CreateRemoteThread(
            processHandle,
            nullptr,
            0,
            remoteFunc,
            nullptr,
            0,
            nullptr)
    };
    if (!remoteThread) {
        return StatusCode::BootstrapInitFailed;
    }

    // Don't wait — the IPC handshake will synchronize
    // The thread handle is released when unique_handle goes out of scope,
    // but the remote thread continues running.
    return StatusCode::Ok;
}

} // namespace z3lx::launcher
