#pragma once

#include "runtime/HookManager.hpp"

#include <functional>
#include <stdexcept>

namespace z3lx::runtime {

template <typename Ret, typename... Args>
Ret HookManager::CallOriginal(Args... args) const {
    if (!trampoline_) {
        throw std::logic_error { "HookManager::CallOriginal: hook not installed" };
    }
    using FnPtr = Ret(*)(Args...);
    return reinterpret_cast<FnPtr>(trampoline_)(args...);
}

} // namespace z3lx::runtime
