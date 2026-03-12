#include "runtime/RuntimeState.hpp"

namespace z3lx::runtime {

RuntimeState::RuntimeState() noexcept
    : currentState { State::Created } {}

RuntimeState::~RuntimeState() noexcept = default;

State RuntimeState::GetState() const noexcept {
    return currentState;
}

bool RuntimeState::CanTransitionTo(const State newState) const noexcept {
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
        return currentState == State::Created;
    case State::IpcReady:
        return currentState == State::HostValidated;
    case State::ConfigReady:
        return currentState == State::IpcReady ||
               currentState == State::HostValidated;
    case State::SymbolsResolved:
        return currentState == State::ConfigReady;
    case State::HooksInstalled:
        return currentState == State::SymbolsResolved;
    case State::Running:
        return currentState == State::HooksInstalled;
    default:
        return false;
    }
}

bool RuntimeState::TransitionTo(const State newState) noexcept {
    if (!CanTransitionTo(newState)) {
        return false;
    }
    currentState = newState;
    return true;
}

bool RuntimeState::IsTerminal() const noexcept {
    return currentState == State::Shutdown ||
           currentState == State::Fatal;
}

} // namespace z3lx::runtime
