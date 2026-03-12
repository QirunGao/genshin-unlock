#include "runtime/FpsService.hpp"

#include <algorithm>

#include <Windows.h>

namespace z3lx::runtime {

FpsService::FpsService() noexcept = default;
FpsService::~FpsService() noexcept = default;

StatusCode FpsService::Initialize(int* targetFpsAddress) {
    if (!targetFpsAddress) {
        return StatusCode::SymbolResolveFailed;
    }
    targetFpsPtr = targetFpsAddress;
    available = true;
    return StatusCode::Ok;
}

void FpsService::SetEnabled(const bool enable) noexcept {
    enabled = enable;
}

void FpsService::SetTargetFps(const int fps) noexcept {
    targetFps = fps;
}

void FpsService::SetAutoThrottle(const bool enable) noexcept {
    autoThrottle = enable;
}

void FpsService::Update() noexcept {
    if (!available || !enabled || !targetFpsPtr) {
        return;
    }

    if (autoThrottle) {
        ApplyThrottling();
    } else {
        *targetFpsPtr = targetFps;
    }
}

bool FpsService::IsEnabled() const noexcept {
    return enabled;
}

bool FpsService::IsAvailable() const noexcept {
    return available;
}

int FpsService::GetTargetFps() const noexcept {
    return targetFps;
}

void FpsService::ApplyThrottling() noexcept {
    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) {
        return;
    }

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    const DWORD currentProcessId = GetCurrentProcessId();
    const bool isFocused = (foregroundProcessId == currentProcessId);

    constexpr int idleTargetFps = 10;
    *targetFpsPtr = isFocused
        ? targetFps
        : (std::min)(targetFps, idleTargetFps);

    const DWORD priority = isFocused
        ? NORMAL_PRIORITY_CLASS : IDLE_PRIORITY_CLASS;
    SetPriorityClass(GetCurrentProcess(), priority);
}

} // namespace z3lx::runtime
