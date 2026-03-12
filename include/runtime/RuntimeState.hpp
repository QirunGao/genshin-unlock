#pragma once

#include <cstdint>
#include <string_view>

namespace z3lx::runtime {

enum class State : uint8_t {
    Created = 0,
    HostValidated,
    IpcReady,
    ConfigReady,
    SymbolsResolved,
    HooksInstalled,
    Running,
    Degraded,
    Shutdown,
    Fatal
};

constexpr std::string_view StateToString(const State state) noexcept {
    switch (state) {
    case State::Created:         return "Created";
    case State::HostValidated:   return "HostValidated";
    case State::IpcReady:        return "IpcReady";
    case State::ConfigReady:     return "ConfigReady";
    case State::SymbolsResolved: return "SymbolsResolved";
    case State::HooksInstalled:  return "HooksInstalled";
    case State::Running:         return "Running";
    case State::Degraded:        return "Degraded";
    case State::Shutdown:        return "Shutdown";
    case State::Fatal:           return "Fatal";
    default:                     return "Unknown";
    }
}

class RuntimeState {
public:
    RuntimeState() noexcept;
    ~RuntimeState() noexcept;

    [[nodiscard]] State GetState() const noexcept;
    bool TransitionTo(State newState) noexcept;

    [[nodiscard]] bool CanTransitionTo(State newState) const noexcept;
    [[nodiscard]] bool IsTerminal() const noexcept;

private:
    State currentState;
};

} // namespace z3lx::runtime
