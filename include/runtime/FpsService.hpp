#pragma once

#include "shared/ConfigModel.hpp"

namespace z3lx::runtime {

// Controls the game's FPS cap by writing to the target integer variable.
class FpsService {
public:
    FpsService() noexcept = default;

    // Set the FPS variable pointer (resolved by MemoryResolver).
    void SetTarget(int* fpsPtr) noexcept;

    // Apply current config settings.
    void ApplyConfig(const z3lx::shared::RuntimeConfig& config) noexcept;

    // Called on every runtime tick.
    void Tick(bool windowFocused) noexcept;

    [[nodiscard]] bool IsActive() const noexcept;

private:
    int*  fpsPtr_      = nullptr;
    bool  enabled_     = false;
    int   targetFps_   = 60;
    bool  autoThrottle_ = false;
};

} // namespace z3lx::runtime
