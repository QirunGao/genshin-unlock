#pragma once

#include <Windows.h>

namespace z3lx::runtime {

// Manages the lifecycle of a single function hook.
// Wraps mmh::Hook but exposes only the operations needed by the runtime.
class HookManager {
public:
    HookManager() noexcept = default;
    ~HookManager() noexcept;

    HookManager(const HookManager&)            = delete;
    HookManager& operator=(const HookManager&) = delete;

    // Install a trampoline hook: redirects the function at `target` to
    // `detour`.  Can be called multiple times if target/detour change.
    void Install(void* target, void* detour);

    // Enable or disable the hook without uninstalling it.
    void SetEnabled(bool enabled);

    // Permanently remove the hook and release resources.
    void Uninstall() noexcept;

    [[nodiscard]] bool IsInstalled() const noexcept;
    [[nodiscard]] bool IsEnabled()   const noexcept;

    // Call the original (unhooked) function.
    template <typename Ret, typename... Args>
    Ret CallOriginal(Args... args) const;

private:
    void*  target_  = nullptr;
    void*  detour_  = nullptr;
    void*  trampoline_ = nullptr;
    bool   enabled_ = false;
};

} // namespace z3lx::runtime

#include "runtime/HookManagerInl.hpp"
