#pragma once

#include "shared/ConfigModel.hpp"

#include <mutex>

namespace z3lx::runtime {

// Controls the FOV by hooking the game's SetFieldOfView function.
// Thread-safe: the hook fires on the game's render thread.
class FovService {
public:
    FovService() noexcept = default;
    ~FovService() noexcept;

    // Set the function address to hook (resolved by MemoryResolver).
    void SetTarget(void* fovFunc);

    // Apply current config settings.
    void ApplyConfig(const z3lx::shared::RuntimeConfig& config) noexcept;

    // Hook activation: enabled when the window is focused and cursor hidden.
    void SetHookActive(bool active);

    [[nodiscard]] bool IsActive() const noexcept;

    // Called from the hook detour.  Must be callable from any thread.
    static float OnSetFieldOfView(void* instance, float value) noexcept;

private:
    void* fovFunc_ = nullptr;
    bool  enabled_ = false;
    int   targetFov_ = 45;
    float smoothing_ = 0.0f;
    bool  hookActive_ = false;
};

} // namespace z3lx::runtime
