#include "runtime/FovService.hpp"

#include <bit>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include <Windows.h>

import mmh;

namespace z3lx::runtime {

struct FovService::Impl {
    mmh::Hook<void, void*, float> hook {};
    std::mutex hookMutex {};
};

namespace {

std::atomic<FovService*>& ActiveServiceStorage() noexcept {
    static std::atomic<FovService*> activeService = nullptr;
    return activeService;
}

} // namespace

FovService::FovService() noexcept
    : stateMutex { std::make_unique<std::mutex>() }
    , impl { std::make_unique<Impl>() } {}

FovService::~FovService() noexcept {
    Shutdown();
}

FovService::FovService(FovService&& other) noexcept
    : state { other.state }
    , filter { std::move(other.filter) }
    , stateMutex { std::move(other.stateMutex) }
    , impl { std::move(other.impl) } {
    other.state = {};
    if (GetActiveInstance() == &other) {
        SetActiveInstance(this);
    }
}

FovService& FovService::operator=(FovService&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Shutdown();

    state = other.state;
    filter = std::move(other.filter);
    stateMutex = std::move(other.stateMutex);
    impl = std::move(other.impl);
    other.state = {};
    if (GetActiveInstance() == &other) {
        SetActiveInstance(this);
    }
    return *this;
}

void FovService::SetActiveInstance(FovService* service) noexcept {
    ActiveServiceStorage().store(service);
}

FovService* FovService::GetActiveInstance() noexcept {
    return ActiveServiceStorage().load();
}

StatusCode FovService::Initialize(void* fovTarget) {
    if (!fovTarget) {
        return StatusCode::SymbolResolveFailed;
    }

    if (!impl) {
        impl = std::make_unique<Impl>();
    }

    const auto detour = reinterpret_cast<void*>(HookCallback);

    std::lock_guard lock { impl->hookMutex };
    impl->hook = mmh::Hook<void, void*, float>::Create(fovTarget, detour);
    if (!impl->hook.IsCreated()) {
        return StatusCode::HookInstallFailed;
    }

    std::lock_guard stateLock { *stateMutex };
    SetActiveInstance(this);
    state.available = true;
    return StatusCode::Ok;
}

void FovService::Shutdown() noexcept {
    if (!impl) {
        return;
    }

    std::lock_guard lock { impl->hookMutex };
    if (impl->hook.IsCreated()) {
        impl->hook.Enable(false);
        impl->hook = {};
    }
    std::lock_guard stateLock { *stateMutex };
    if (GetActiveInstance() == this) {
        SetActiveInstance(nullptr);
    }
    state = {};
}

void FovService::SetEnabled(const bool enable) noexcept {
    if (!impl) {
        std::lock_guard lock { *stateMutex };
        state.enabled = enable;
        return;
    }

    std::lock_guard hookLock { impl->hookMutex };
    std::lock_guard stateLock { *stateMutex };
    if (!enable && state.hooked && impl->hook.IsCreated()) {
        impl->hook.Enable(false);
        state.hooked = false;
    }
    state.enabled = enable;
}

void FovService::SetTargetFov(const int fov) noexcept {
    std::lock_guard lock { *stateMutex };
    state.targetFov = fov;
}

void FovService::SetSmoothing(const float smoothing) noexcept {
    std::lock_guard lock { *stateMutex };
    filter.SetTimeConstant(smoothing);
}

void FovService::Update() noexcept {
    if (!impl) {
        return;
    }

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

    std::lock_guard hookLock { impl->hookMutex };
    std::lock_guard stateLock { *stateMutex };
    if (!state.available) {
        return;
    }

    const bool shouldHook = state.enabled && isFocused && !cursorVisible;
    if (shouldHook && !state.hooked) {
        if (impl->hook.IsCreated()) {
            impl->hook.Enable(true);
            state.enabledOnce = true;
        }
        state.hooked = true;
    } else if (!shouldHook && state.hooked) {
        state.hooked = false;
        if (impl->hook.IsCreated()) {
            impl->hook.Enable(false);
        }
    }
}

void FovService::HandleHookCallback(
    void* instance, float& value) noexcept {
    std::lock_guard stateLock { *stateMutex };
    ++state.setFovCount;

    if (const bool isDefaultFov = value == 45.0f;
        instance == state.previousInstance &&
        (value == state.previousFov || isDefaultFov)) {
        if (isDefaultFov) {
            state.previousInstance = instance;
            state.previousFov = value;
        }

        if (state.setFovCount > 8) {
            filter.SetInitialValue(value);
        }
        state.setFovCount = 0;

        if (state.enabledOnce) {
            state.enabledOnce = false;
            filter.Update(value);
        }
        const float target = (state.hooked && state.enabled)
            ? static_cast<float>(state.targetFov)
            : state.previousFov;
        const float filtered = filter.Update(target);

        if ((state.hooked && state.enabled) || !state.isPreviousFov) {
            state.isPreviousFov =
                std::abs(state.previousFov - filtered) < 0.1f;
            value = filtered;
        } else if (!state.hooked && impl && impl->hook.IsCreated()) {
            state.isPreviousFov = false;
            impl->hook.Enable(false);
        }
    } else {
        const auto rep = std::bit_cast<std::uint32_t>(value);
        value = std::bit_cast<float>(rep + 1);
        state.previousInstance = instance;
        state.previousFov = value;
    }
}

bool FovService::IsEnabled() const noexcept {
    std::lock_guard lock { *stateMutex };
    return state.enabled;
}

bool FovService::IsAvailable() const noexcept {
    std::lock_guard lock { *stateMutex };
    return state.available;
}

bool FovService::IsHooked() const noexcept {
    std::lock_guard lock { *stateMutex };
    return state.hooked;
}

int FovService::GetTargetFov() const noexcept {
    std::lock_guard lock { *stateMutex };
    return state.targetFov;
}

void FovService::HookCallback(
    void* instance, float value) noexcept try {
    auto* service = GetActiveInstance();
    if (!service || !service->impl) {
        return;
    }

    std::lock_guard lock { service->impl->hookMutex };
    if (!service->impl->hook.IsCreated()) {
        return;
    }

    service->HandleHookCallback(instance, value);
    service->impl->hook.CallOriginal(instance, value);
} catch (...) {
    if (auto* service = z3lx::runtime::FovService::GetActiveInstance();
        service && service->impl && service->impl->hook.IsCreated()) {
        service->impl->hook.CallOriginal(instance, value);
    }
}

} // namespace z3lx::runtime
