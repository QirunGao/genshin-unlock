#pragma once

#include "shared/ConfigModel.hpp"
#include "shared/VersionTable.hpp"

#include <optional>

namespace z3lx::runtime {

// Resolves absolute memory addresses for FPS and FOV hooks.
// Uses the version whitelist (shared/VersionTable.hpp) to select offsets.
class MemoryResolver {
public:
    struct Addresses {
        int*  fpsTarget = nullptr;   // Pointer to the FPS integer variable
        void* fovFunc   = nullptr;   // Pointer to the SetFieldOfView function
    };

    // Resolve addresses for the given game version.
    // Returns nullopt when the version is not in the whitelist.
    [[nodiscard]] static std::optional<Addresses> Resolve(
        uint16_t gameVersionMajor,
        uint16_t gameVersionMinor);
};

} // namespace z3lx::runtime
