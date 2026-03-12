#include "runtime/HookManager.hpp"

#include <wil/result.h>

#include <stdexcept>

#include <Windows.h>

import mmh;

// HookManager wraps mmh::Hook<void, void*, float> (the FOV hook) in a
// type-erased handle so higher-level code doesn't depend on mmh directly.

namespace z3lx::runtime {

namespace {
mmh::Hook<void, void*, float> s_hook {};
} // namespace

HookManager::~HookManager() noexcept {
    Uninstall();
}

void HookManager::Install(void* const target, void* const detour) {
    Uninstall();
    s_hook      = mmh::Hook<void, void*, float>::Create(target, detour);
    target_     = target;
    detour_     = detour;
    trampoline_ = target; // non-null sentinel; actual trampoline owned by s_hook
    enabled_    = false;
}

void HookManager::SetEnabled(const bool enabled) {
    if (enabled == enabled_ || !s_hook.IsCreated()) {
        return;
    }
    enabled_ = enabled;
    s_hook.Enable(enabled);
}

void HookManager::Uninstall() noexcept {
    if (s_hook.IsCreated()) {
        s_hook = {};
    }
    target_     = nullptr;
    detour_     = nullptr;
    trampoline_ = nullptr;
    enabled_    = false;
}

bool HookManager::IsInstalled() const noexcept {
    return trampoline_ != nullptr;
}

bool HookManager::IsEnabled() const noexcept {
    return enabled_;
}

} // namespace z3lx::runtime
