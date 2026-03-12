#pragma once

#include "shared/StatusCode.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace z3lx::runtime {

using namespace z3lx::shared;

struct HookDefinition {
    std::string name;
    void* target = nullptr;
    void* detour = nullptr;
};

struct HookEntry {
    HookDefinition definition;
    bool installed = false;
    bool enabled = false;
};

class HookManager {
public:
    HookManager() noexcept;
    ~HookManager() noexcept;

    StatusCode Register(const HookDefinition& definition);
    StatusCode InstallAll();
    StatusCode UninstallAll();

    StatusCode Enable(const std::string& name);
    StatusCode Disable(const std::string& name);
    StatusCode EnableAll();
    StatusCode DisableAll();

    [[nodiscard]] bool IsInstalled(const std::string& name) const noexcept;
    [[nodiscard]] bool IsEnabled(const std::string& name) const noexcept;

    using StateChangeCallback = std::function<void(const std::string& name, bool installed, bool enabled)>;
    void SetStateChangeCallback(StateChangeCallback callback);

private:
    std::vector<HookEntry> hooks;
    StateChangeCallback stateChangeCallback;
};

} // namespace z3lx::runtime
