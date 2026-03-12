#include "runtime/FovService.hpp"
#include "util/ExponentialFilter.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>

#include <Windows.h>

import mmh;

namespace {
// Singleton bridge: the static hook callback forwards to this instance.
z3lx::runtime::FovService* g_activeService = nullptr;
mmh::Hook<void, void*, float> g_fovHook {};
std::mutex g_hookMutex {};

void HkSetFieldOfView(void* instance, float value) noexcept;
} // namespace

namespace z3lx::runtime {

FovService::FovService() noexcept = default;

FovService::~FovService() noexcept {
    std::lock_guard lock { g_hookMutex };
    g_fovHook = {};
    g_activeService = nullptr;
}

StatusCode FovService::Initialize(void* fovTarget) {
    if (!fovTarget) {
        return StatusCode::SymbolResolveFailed;
    }

    const auto detour = reinterpret_cast<void*>(HkSetFieldOfView);

    std::lock_guard lock { g_hookMutex };
    g_fovHook = mmh::Hook<void, void*, float>::Create(fovTarget, detour);
    g_activeService = this;
    available = true;
    return StatusCode::Ok;
}

void FovService::SetEnabled(const bool enable) noexcept {
    std::lock_guard lock { mutex };
    enabled = enable;
}

void FovService::SetTargetFov(const int fov) noexcept {
    std::lock_guard lock { mutex };
    targetFov = fov;
}

void FovService::SetSmoothing(const float smoothing) noexcept {
    std::lock_guard lock { mutex };
    filter.SetTimeConstant(smoothing);
}

void FovService::Update() noexcept {
    if (!available) return;

    const HWND foregroundWindow = GetForegroundWindow();
    bool isFocused = false;
    if (foregroundWindow) {
        DWORD foregroundProcessId = 0;
        GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
        isFocused = (foregroundProcessId == GetCurrentProcessId());
    }

    CURSORINFO cursorInfo { .cbSize = sizeof(cursorInfo) };
    bool cursorVisible = true;
    if (GetCursorInfo(&cursorInfo)) {
        cursorVisible = (cursorInfo.flags & CURSOR_SHOWING) != 0;
    }

    const bool shouldHook = isFocused && !cursorVisible;

    std::lock_guard lock { g_hookMutex };
    if (shouldHook && !hooked) {
        if (g_fovHook.IsCreated()) {
            g_fovHook.Enable(true);
            enabledOnce = true;
        }
        hooked = true;
    } else if (!shouldHook && hooked) {
        hooked = false;
    }
}

void FovService::HandleHookCallback(
    void* instance, float& value) noexcept {
    // Called under g_hookMutex from the trampoline
    ++setFovCount;

    if (const bool isDefaultFov = value == 45.0f;
        instance == previousInstance &&
        (value == previousFov || isDefaultFov)) {
        if (isDefaultFov) {
            previousInstance = instance;
            previousFov = value;
        }

        if (setFovCount > 8) {
            filter.SetInitialValue(value);
        }
        setFovCount = 0;

        if (enabledOnce) {
            enabledOnce = false;
            filter.Update(value);
        }
        const float target = (hooked && enabled)
            ? static_cast<float>(targetFov)
            : previousFov;
        const float filtered = filter.Update(target);

        if ((hooked && enabled) || !isPreviousFov) {
            isPreviousFov =
                std::abs(previousFov - filtered) < 0.1f;
            value = filtered;
        } else if (!hooked) {
            isPreviousFov = false;
            g_fovHook.Enable(false);
        }
    } else {
        const auto rep = std::bit_cast<std::uint32_t>(value);
        value = std::bit_cast<float>(rep + 1);
        previousInstance = instance;
        previousFov = value;
    }
}

bool FovService::IsEnabled() const noexcept {
    return enabled;
}

bool FovService::IsAvailable() const noexcept {
    return available;
}

bool FovService::IsHooked() const noexcept {
    return hooked;
}

int FovService::GetTargetFov() const noexcept {
    return targetFov;
}

} // namespace z3lx::runtime

namespace {
void HkSetFieldOfView(void* instance, float value) noexcept try {
    std::lock_guard lock { g_hookMutex };
    if (!g_fovHook.IsCreated() || !g_activeService) {
        return;
    }

    g_activeService->HandleHookCallback(instance, value);
    g_fovHook.CallOriginal(instance, value);
} catch (...) {
    g_fovHook.CallOriginal(instance, value);
}
} // namespace
