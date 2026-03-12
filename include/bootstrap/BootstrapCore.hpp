#pragma once

#include <Windows.h>

namespace z3lx::bootstrap {

// Bootstrap worker entry point. Runs in a dedicated thread started from
// DllMain, after DllMain returns and the loader lock has been released.
//
// Lifecycle:
//   1. Connects to the launcher's named pipe.
//   2. Validates the host process (module name / path checks).
//   3. Reports BootstrapReady to the launcher.
//   4. Receives the path to runtime.dll and loads it.
//   5. Receives the ConfigSnapshot and calls RuntimeInitialize.
//   6. Reports RuntimeInitResult.
//   7. Forwards heartbeat / error messages while the runtime is active.
void RunBootstrap(HMODULE hModule);

} // namespace z3lx::bootstrap
