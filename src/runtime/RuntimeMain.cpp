#include "runtime/RuntimeMain.hpp"
#include "runtime/RuntimeState.hpp"
#include "shared/Protocol.hpp"

#include <wil/result.h>

#include <memory>

#include <Windows.h>

// ── DllMain ───────────────────────────────────────────────────────────────────
// Intentionally minimal: only saves the module handle.
// All initialisation is performed by RuntimeInitialize, called explicitly.

static HMODULE g_hModule = nullptr;

BOOL WINAPI DllMain(
    const HINSTANCE hinstDLL,
    const DWORD     fdwReason,
    const LPVOID    /* lpReserved */) noexcept
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        g_hModule = hinstDLL;
    }
    return TRUE;
}

// ── RuntimeInitialize ─────────────────────────────────────────────────────────
// Exported function.  Called from bootstrap.dll after LoadLibraryW.
// Receives the frozen config snapshot; does NOT read files.

static std::unique_ptr<z3lx::runtime::RuntimeState> g_state {};

extern "C" __declspec(dllexport)
z3lx::shared::RuntimeInitResult __cdecl RuntimeInitialize(
    const z3lx::shared::RuntimeInitParams* const params) try
{
    if (!params) {
        return { z3lx::shared::StatusCode::RuntimeInitFailed, false, false };
    }

    g_state = std::make_unique<z3lx::runtime::RuntimeState>();

    const bool ok = g_state->Initialise(
        params->config,
        params->gameVersionMajor,
        params->gameVersionMinor
    );

    if (!ok) {
        g_state.reset();
        return { z3lx::shared::StatusCode::SymbolResolveFailed, false, false };
    }

    g_state->Start();

    return {
        z3lx::shared::StatusCode::Ok,
        g_state->IsFpsAvailable(),
        g_state->IsFovAvailable(),
    };
} catch (...) {
    LOG_CAUGHT_EXCEPTION();
    g_state.reset();
    return { z3lx::shared::StatusCode::RuntimeInitFailed, false, false };
}
