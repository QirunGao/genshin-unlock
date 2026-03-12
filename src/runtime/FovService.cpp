#include "runtime/FovService.hpp"
#include "util/ExponentialFilter.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>

#include <Windows.h>

import mmh;

namespace {
// Global state for the FOV hook callback
std::mutex g_fovMutex {};
mmh::Hook<void, void*, float> g_fovHook {};
z3lx::runtime::FovRuntimeState g_fovState {};

void HkSetFieldOfView(void* instance, float value) noexcept;
} // namespace

namespace z3lx::runtime {

FovService::FovService() noexcept = default;

FovService::~FovService() noexcept {
    std::lock_guard lock { g_fovMutex };
    g_fovHook = {};
}

StatusCode FovService::Initialize(void* fovTarget) {
    if (!fovTarget) {
        return StatusCode::SymbolResolveFailed;
    }

    const auto detour = reinterpret_cast<void*>(HkSetFieldOfView);

    std::lock_guard lock { g_fovMutex };
    g_fovHook = mmh::Hook<void, void*, float>::Create(fovTarget, detour);
    available = true;
    return StatusCode::Ok;
}

void FovService::SetEnabled(const bool enable) noexcept {
    std::lock_guard lock { g_fovMutex };
    g_fovState.enabled = enable;
}

void FovService::SetTargetFov(const int fov) noexcept {
    std::lock_guard lock { g_fovMutex };
    g_fovState.targetFov = fov;
}

void FovService::SetSmoothing(const float smoothing) noexcept {
    std::lock_guard lock { g_fovMutex };
    g_fovState.filter.SetTimeConstant(smoothing);
}

void FovService::SetHooked(const bool hooked) noexcept {
    std::lock_guard lock { g_fovMutex };
    g_fovState.hooked = hooked;
}

void FovService::Update() noexcept {
    if (!available) return;

    // Check window focus and cursor visibility to decide hook state
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

    std::lock_guard lock { g_fovMutex };
    if (shouldHook && !g_fovState.hooked) {
        if (g_fovHook.IsCreated()) {
            g_fovHook.Enable(true);
            g_fovState.enabledOnce = true;
        }
        g_fovState.hooked = true;
    } else if (!shouldHook && g_fovState.hooked) {
        g_fovState.hooked = false;
    }
}

bool FovService::IsEnabled() const noexcept {
    return g_fovState.enabled;
}

bool FovService::IsAvailable() const noexcept {
    return available;
}

bool FovService::IsHooked() const noexcept {
    return g_fovState.hooked;
}

int FovService::GetTargetFov() const noexcept {
    return g_fovState.targetFov;
}

} // namespace z3lx::runtime

namespace {
void HkSetFieldOfView(void* instance, float value) noexcept try {
    std::lock_guard lock { g_fovMutex };
    if (!g_fovHook.IsCreated()) {
        return;
    }

    auto& st = g_fovState;
    ++st.setFovCount;

    if (const bool isDefaultFov = value == 45.0f;
        instance == st.previousInstance &&
        (value == st.previousFov || isDefaultFov)) {
        if (isDefaultFov) {
            st.previousInstance = instance;
            st.previousFov = value;
        }

        if (st.setFovCount > 8) {
            st.filter.SetInitialValue(value);
        }
        st.setFovCount = 0;

        if (st.enabledOnce) {
            st.enabledOnce = false;
            st.filter.Update(value);
        }
        const float target = (st.hooked && st.enabled) ?
            static_cast<float>(st.targetFov) : st.previousFov;
        const float filtered = st.filter.Update(target);

        if ((st.hooked && st.enabled) || !st.isPreviousFov) {
            st.isPreviousFov = std::abs(st.previousFov - filtered) < 0.1f;
            value = filtered;
        } else if (!st.hooked) {
            st.isPreviousFov = false;
            g_fovHook.Enable(false);
        }
    } else {
        const auto rep = std::bit_cast<std::uint32_t>(value);
        value = std::bit_cast<float>(rep + 1); // marker value
        st.previousInstance = instance;
        st.previousFov = value;
    }

    g_fovHook.CallOriginal(instance, value);
} catch (...) {
    g_fovHook.CallOriginal(instance, value);
}
} // namespace
