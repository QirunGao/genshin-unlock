#include "bootstrap/BootstrapCore.hpp"
#include "shared/Protocol.hpp"

#include <wil/result.h>

#include <thread>

#include <Windows.h>

namespace {
HMODULE g_hModule = nullptr;

void BootstrapWorkerEntry() try {
    z3lx::bootstrap::RunBootstrap(g_hModule);
} CATCH_LOG()
} // namespace

// DllMain MUST be kept minimal.  Only:
//   1. Save the module handle.
//   2. Disable thread-attach / detach notifications.
//   3. Start the deferred initialisation thread.
//
// No file I/O, no JSON, no complex logging, no hook installation.
BOOL WINAPI DllMain(
    const HINSTANCE hinstDLL,
    const DWORD     fdwReason,
    const LPVOID    /* lpReserved */) try
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        g_hModule = hinstDLL;
        std::thread { BootstrapWorkerEntry }.detach();
    }
    return TRUE;
} catch (...) {
    LOG_CAUGHT_EXCEPTION();
    return FALSE;
}
