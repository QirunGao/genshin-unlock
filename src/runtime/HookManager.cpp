#include "runtime/HookManager.hpp"

import mmh;

#include <algorithm>
#include <ranges>
#include <string>

namespace z3lx::runtime {

HookManager::HookManager() noexcept = default;
HookManager::~HookManager() noexcept {
    UninstallAll();
}

StatusCode HookManager::Register(const HookDefinition& definition) {
    hooks.push_back(HookEntry {
        .definition = definition,
        .installed = false,
        .enabled = false
    });
    return StatusCode::Ok;
}

StatusCode HookManager::RegisterHook(const std::string& name) {
    return Register(HookDefinition { .name = name });
}

StatusCode HookManager::SetHookState(const std::string& name,
    const bool installed, const bool enabled) {
    auto it = std::ranges::find_if(hooks,
        [&](const HookEntry& e) { return e.definition.name == name; });
    if (it == hooks.end()) return StatusCode::HookInstallFailed;

    it->installed = installed;
    it->enabled = enabled;
    if (stateChangeCallback) {
        stateChangeCallback(name, installed, enabled);
    }
    return StatusCode::Ok;
}

StatusCode HookManager::InstallAll() {
    for (auto& entry : hooks) {
        if (entry.installed) continue;
        // Use lifecycle callback if provided, otherwise use target+detour presence.
        if (entry.definition.installFn) {
            entry.installed = entry.definition.installFn();
        } else if (entry.definition.target && entry.definition.detour) {
            entry.installed = true;
        }
        if (entry.installed && stateChangeCallback) {
            stateChangeCallback(
                entry.definition.name, entry.installed, entry.enabled);
        }
    }
    return StatusCode::Ok;
}

StatusCode HookManager::UninstallAll() {
    for (auto& entry : hooks) {
        if (entry.definition.uninstallFn && entry.installed) {
            entry.definition.uninstallFn();
        }
        entry.installed = false;
        entry.enabled = false;
        if (stateChangeCallback) {
            stateChangeCallback(
                entry.definition.name, entry.installed, entry.enabled);
        }
    }
    return StatusCode::Ok;
}

StatusCode HookManager::Enable(const std::string& name) {
    auto it = std::ranges::find_if(hooks,
        [&](const HookEntry& e) { return e.definition.name == name; });
    if (it == hooks.end()) return StatusCode::HookInstallFailed;
    if (!it->installed) return StatusCode::HookInstallFailed;

    it->enabled = true;
    if (it->definition.setEnabledFn) {
        it->definition.setEnabledFn(true);
    }
    if (stateChangeCallback) {
        stateChangeCallback(name, it->installed, it->enabled);
    }
    return StatusCode::Ok;
}

StatusCode HookManager::Disable(const std::string& name) {
    auto it = std::ranges::find_if(hooks,
        [&](const HookEntry& e) { return e.definition.name == name; });
    if (it == hooks.end()) return StatusCode::HookInstallFailed;

    it->enabled = false;
    if (it->definition.setEnabledFn) {
        it->definition.setEnabledFn(false);
    }
    if (stateChangeCallback) {
        stateChangeCallback(name, it->installed, it->enabled);
    }
    return StatusCode::Ok;
}

StatusCode HookManager::EnableAll() {
    for (auto& entry : hooks) {
        if (entry.installed) {
            entry.enabled = true;
            if (entry.definition.setEnabledFn) {
                entry.definition.setEnabledFn(true);
            }
            if (stateChangeCallback) {
                stateChangeCallback(
                    entry.definition.name, entry.installed, entry.enabled);
            }
        }
    }
    return StatusCode::Ok;
}

StatusCode HookManager::DisableAll() {
    for (auto& entry : hooks) {
        entry.enabled = false;
        if (entry.definition.setEnabledFn) {
            entry.definition.setEnabledFn(false);
        }
        if (stateChangeCallback) {
            stateChangeCallback(
                entry.definition.name, entry.installed, entry.enabled);
        }
    }
    return StatusCode::Ok;
}

bool HookManager::IsInstalled(const std::string& name) const noexcept {
    auto it = std::ranges::find_if(hooks,
        [&](const HookEntry& e) { return e.definition.name == name; });
    return it != hooks.end() && it->installed;
}

bool HookManager::IsEnabled(const std::string& name) const noexcept {
    auto it = std::ranges::find_if(hooks,
        [&](const HookEntry& e) { return e.definition.name == name; });
    return it != hooks.end() && it->enabled;
}

void HookManager::SetStateChangeCallback(StateChangeCallback callback) {
    stateChangeCallback = std::move(callback);
}

} // namespace z3lx::runtime
