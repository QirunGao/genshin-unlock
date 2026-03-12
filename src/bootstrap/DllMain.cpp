#include <Windows.h>

namespace {
HMODULE g_bootstrapModule = nullptr;
} // namespace

BOOL WINAPI DllMain(
    const HINSTANCE hinstDLL,
    const DWORD fdwReason,
    [[maybe_unused]] const LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_bootstrapModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

namespace z3lx::bootstrap {
HMODULE GetBootstrapModule() noexcept {
    return g_bootstrapModule;
}
} // namespace z3lx::bootstrap
