#include "runtime/FpsService.hpp"

#include <algorithm>

#include <Windows.h>

namespace z3lx::runtime {

namespace {

constexpr int kUnlimitedFps = -1;
constexpr int kMinTargetFps = 1;
constexpr int kMaxTargetFps = 1000;

int SanitizeTargetFps(const int fps) noexcept {
    if (fps == kUnlimitedFps) {
        return fps;
    }
    return (std::clamp)(fps, kMinTargetFps, kMaxTargetFps);
}

} // namespace

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

void FpsService::Shutdown() noexcept {
    enabled = false;
    available = false;
    targetFpsPtr = nullptr;
}

void FpsService::SetEnabled(const bool enable) noexcept {
    enabled = enable;
}

void FpsService::SetTargetFps(const int fps) noexcept {
    targetFps = SanitizeTargetFps(fps);
}

void FpsService::SetAutoThrottle(const bool enable) noexcept {
    autoThrottle = enable;
}

void FpsService::Update() noexcept {
    if (!available || !enabled || !targetFpsPtr) {
        return;
    }

    const int requestedFps = SanitizeTargetFps(targetFps);
    if (autoThrottle) {
        *targetFpsPtr = ApplyThrottling(requestedFps);
    } else {
        *targetFpsPtr = requestedFps;
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

int FpsService::ApplyThrottling(const int requestedFps) const noexcept {
    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) {
        return requestedFps;
    }

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    const DWORD currentProcessId = GetCurrentProcessId();
    const bool isFocused = (foregroundProcessId == currentProcessId);

    constexpr int idleTargetFps = 10;
    if (isFocused) {
        return requestedFps;
    }
    if (requestedFps == kUnlimitedFps) {
        return idleTargetFps;
    }
    return (std::min)(requestedFps, idleTargetFps);
}

} // namespace z3lx::runtime
