#pragma once

#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"

#include <Windows.h>

// Exported entry point.  Called by the bootstrap after LoadLibraryW.
// RuntimeInitParams and RuntimeInitResult are defined in shared/Protocol.hpp.
extern "C" __declspec(dllexport)
z3lx::shared::RuntimeInitResult __cdecl
RuntimeInitialize(const z3lx::shared::RuntimeInitParams* params);
