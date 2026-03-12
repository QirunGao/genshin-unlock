#include "runtime/FovService.hpp"
#include "util/ExponentialFilter.hpp"

#include <bit>
#include <cmath>
#include <mutex>

#include <Windows.h>

import mmh;

namespace {

// Global hook state – accessed from the game render thread via the detour.
std::mutex g_mutex {};
mmh::Hook<void, void*, float> g_hook {};

bool   g_isEnabled      = false;
bool   g_isHookActive   = false;
bool   g_isEnabledOnce  = false;
int    g_targetFov      = 45;
float  g_smoothing      = 0.0f;

int    g_setFovCount       = 0;
void*  g_prevInstance      = nullptr;
float  g_prevFov           = 45.0f;
bool   g_isPrevFovReached  = false;

z3lx::util::ExponentialFilter<float> g_filter {};

void HkSetFieldOfView(void* instance, float value) noexcept;

} // namespace

namespace z3lx::runtime {

FovService::~FovService() noexcept {
    std::lock_guard lock { g_mutex };
    g_hook = {};
}

void FovService::SetTarget(void* const fovFunc) {
    fovFunc_ = fovFunc;
    if (!fovFunc_) {
        return;
    }
    std::lock_guard lock { g_mutex };
    g_hook = mmh::Hook<void, void*, float>::Create(
        fovFunc_,
        reinterpret_cast<void*>(HkSetFieldOfView)
    );
}

void FovService::ApplyConfig(
    const z3lx::shared::RuntimeConfig& config) noexcept
{
    enabled_   = config.unlockFov;
    targetFov_ = config.targetFov;
    smoothing_ = config.fovSmoothing;

    std::lock_guard lock { g_mutex };
    g_isEnabled  = enabled_;
    g_targetFov  = targetFov_;
    g_filter.SetTimeConstant(smoothing_);
}

void FovService::SetHookActive(const bool active) {
    if (active == hookActive_) {
        return;
    }
    hookActive_ = active;
    std::lock_guard lock { g_mutex };
    g_isHookActive = active;
    if (active) {
        g_hook.Enable(true);
        g_isEnabledOnce = true;
    }
    // Intentionally do NOT call g_hook.Enable(false) when deactivating –
    // the detour handles the transition smoothly.
}

bool FovService::IsActive() const noexcept {
    return fovFunc_ != nullptr && enabled_;
}

float FovService::OnSetFieldOfView(void*, float) noexcept {
    // Static callback forwarded from the hook detour; logic lives there.
    return 0.0f;
}

} // namespace z3lx::runtime

namespace {

void HkSetFieldOfView(void* const instance, float value) noexcept try {
    std::lock_guard lock { g_mutex };
    if (!g_hook.IsCreated()) {
        return;
    }

    ++g_setFovCount;
    if (const bool isDefault = value == 45.0f;
        instance == g_prevInstance &&
        (value == g_prevFov || isDefault))
    {
        if (isDefault) {
            g_prevInstance = instance;
            g_prevFov      = value;
        }
        if (g_setFovCount > 8) {
            g_filter.SetInitialValue(value);
        }
        g_setFovCount = 0;

        if (g_isEnabledOnce) {
            g_isEnabledOnce = false;
            g_filter.Update(value);
        }

        const float target   = (g_isHookActive && g_isEnabled)
                                   ? static_cast<float>(g_targetFov)
                                   : g_prevFov;
        const float filtered = g_filter.Update(target);

        if ((g_isHookActive && g_isEnabled) || !g_isPrevFovReached) {
            g_isPrevFovReached =
                std::abs(g_prevFov - filtered) < 0.1f;
            value = filtered;
        } else if (!g_isHookActive) {
            g_isPrevFovReached = false;
            g_hook.Enable(false);
        }
    } else {
        // Marker: increment representation so game doesn't accept the value.
        const auto rep = std::bit_cast<uint32_t>(value);
        value = std::bit_cast<float>(rep + 1u);
        g_prevInstance = instance;
        g_prevFov      = value;
    }

    g_hook.CallOriginal(instance, value);
} catch (...) {
    g_hook.CallOriginal(instance, value);
}

} // namespace
