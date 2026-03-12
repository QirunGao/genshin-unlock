#pragma once

#include <filesystem>
#include <Windows.h>

namespace z3lx::launcher {

// Injects a DLL into the target process using CreateRemoteThread + LoadLibraryW.
// Waits for the injection thread to finish and verifies the return value.
//
// Design rationale (per architecture document §7.1):
//   - Explicit thread allows WaitForSingleObject for success/failure checking.
//   - GetExitCodeThread validates non-null HMODULE (truncated to 32-bit, but
//     null-check is sufficient for failure detection on 64-bit).
//   - Does NOT use QueueUserAPC – avoids APC scheduling ambiguity.
//
// Security constraints (per §17.1):
//   - Only absolute paths accepted (rejects relative and UNC paths).
//   - Only files with .dll extension accepted.
//   - Callers are responsible for ensuring libraryPath is within the
//     launcher's own directory (enforced at the call site in Main.cpp).
//
// Throws on any failure (THROW_WIN32 / std::runtime_error).
void InjectLibrary(
    HANDLE processHandle,
    const std::filesystem::path& libraryPath);

} // namespace z3lx::launcher
