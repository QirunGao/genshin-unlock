#include "runtime/FpsService.hpp"

#include <algorithm>

#include <Windows.h>

namespace z3lx::runtime {

void FpsService::SetTarget(int* const fpsPtr) noexcept {
    fpsPtr_ = fpsPtr;
}

void FpsService::ApplyConfig(
    const z3lx::shared::RuntimeConfig& config) noexcept
{
    enabled_      = config.unlockFps;
    targetFps_    = config.targetFps;
    autoThrottle_ = config.autoThrottle;
}

void FpsService::Tick(const bool windowFocused) noexcept {
    if (!fpsPtr_ || !enabled_) {
        return;
    }

    if (autoThrottle_) {
        constexpr int IDLE_FPS = 10;
        *fpsPtr_ = windowFocused
            ? targetFps_
            : (std::min)(targetFps_, IDLE_FPS);

        const DWORD priority = windowFocused
            ? NORMAL_PRIORITY_CLASS : IDLE_PRIORITY_CLASS;
        SetPriorityClass(GetCurrentProcess(), priority);
    } else {
        *fpsPtr_ = targetFps_;
    }
}

bool FpsService::IsActive() const noexcept {
    return fpsPtr_ != nullptr && enabled_;
}

} // namespace z3lx::runtime
