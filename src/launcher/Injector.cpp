#include "launcher/Injector.hpp"
#include "util/win/Loader.hpp"

#include <wil/result.h>

#include <filesystem>
#include <format>
#include <stdexcept>

#include <Windows.h>

namespace z3lx::launcher {

void InjectLibrary(
    const HANDLE processHandle,
    const std::filesystem::path& libraryPath)
{
    namespace fs = std::filesystem;

    // ── Security: validate the library path ──────────────────────────────────
    if (!libraryPath.is_absolute()) {
        throw std::invalid_argument {
            std::format("InjectLibrary: path must be absolute: {}",
                        libraryPath.string())
        };
    }
    if (libraryPath.extension() != L".dll") {
        throw std::invalid_argument {
            std::format("InjectLibrary: only .dll files are accepted: {}",
                        libraryPath.string())
        };
    }
    if (!fs::exists(libraryPath) || !fs::is_regular_file(libraryPath)) {
        throw std::runtime_error {
            std::format("InjectLibrary: file not found: {}",
                        libraryPath.string())
        };
    }

    // ── Resolve LoadLibraryW address ─────────────────────────────────────────
    // kernel32.dll is mapped at the same base across all processes in the
    // same Windows session, so the local address is valid in the target.
    const auto loadLibraryAddr = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void*>(&LoadLibraryW)
    );

    // ── Write DLL path into target process memory ─────────────────────────────
    const size_t pathBytes =
        (libraryPath.native().size() + 1) * sizeof(wchar_t);

    const LPVOID remoteBuf = VirtualAllocEx(
        processHandle,
        nullptr,
        pathBytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    THROW_LAST_ERROR_IF_NULL(remoteBuf);

    THROW_IF_WIN32_BOOL_FALSE(WriteProcessMemory(
        processHandle,
        remoteBuf,
        libraryPath.c_str(),
        pathBytes,
        nullptr
    ));

    // ── Launch remote thread ──────────────────────────────────────────────────
    // CreateRemoteThread gives us an explicit handle we can wait on and check.
    DWORD   threadId   = 0;
    HANDLE  rawThread  = CreateRemoteThread(
        processHandle,
        nullptr,
        0,
        loadLibraryAddr,
        remoteBuf,
        0,
        &threadId
    );
    THROW_LAST_ERROR_IF_NULL(rawThread);

    const wil::unique_handle thread { rawThread };

    // Wait for LoadLibraryW to return
    constexpr DWORD INJECT_TIMEOUT_MS = 15'000;
    const DWORD waitResult = WaitForSingleObject(thread.get(), INJECT_TIMEOUT_MS);
    if (waitResult != WAIT_OBJECT_0) {
        THROW_WIN32(waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }

    // GetExitCodeThread returns the HMODULE cast to DWORD.
    // Non-zero means LoadLibraryW succeeded.
    DWORD exitCode = 0;
    THROW_IF_WIN32_BOOL_FALSE(GetExitCodeThread(thread.get(), &exitCode));
    if (exitCode == 0) {
        THROW_WIN32_MSG(ERROR_DLL_INIT_FAILED,
            "LoadLibraryW in target process returned NULL");
    }

    // Free the path buffer (LoadLibraryW has already consumed it)
    VirtualFreeEx(processHandle, remoteBuf, 0, MEM_RELEASE);
}

} // namespace z3lx::launcher
