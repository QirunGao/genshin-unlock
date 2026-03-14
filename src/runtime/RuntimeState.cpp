#include "runtime/RuntimeState.hpp"

namespace z3lx::runtime {

RuntimeState::RuntimeState() noexcept
    : currentState { State::Created } {}

RuntimeState::~RuntimeState() noexcept = default;

State RuntimeState::GetState() const noexcept {
    return currentState.load();
}

bool RuntimeState::CanTransitionTo(const State newState) const noexcept {
    const State current = currentState.load();
    // Allow transitions according to the state machine:
    // Created -> HostValidated -> IpcReady -> ConfigReady ->
    // SymbolsResolved -> HooksInstalled -> Running
    // Any state -> Degraded, Shutdown, Fatal
    switch (newState) {
    case State::Degraded:
    case State::Shutdown:
    case State::Fatal:
        return true;
    case State::HostValidated:
        return current == State::Created;
    case State::IpcReady:
        return current == State::HostValidated;
    case State::ConfigReady:
        return current == State::IpcReady ||
               current == State::HostValidated;
    case State::SymbolsResolved:
        return current == State::ConfigReady;
    case State::HooksInstalled:
        return current == State::SymbolsResolved;
    case State::Running:
        return current == State::HooksInstalled;
    default:
        return false;
    }
}

bool RuntimeState::TransitionTo(const State newState) noexcept {
    if (!CanTransitionTo(newState)) {
        return false;
    }
    currentState.store(newState);
    return true;
}

bool RuntimeState::IsTerminal() const noexcept {
    const State current = currentState.load();
    return current == State::Shutdown ||
           current == State::Fatal;
}

} // namespace z3lx::runtime
