#pragma once

#include "runtime/FpsService.hpp"
#include "runtime/FovService.hpp"
#include "runtime/InputSampler.hpp"
#include "runtime/MemoryResolver.hpp"
#include "shared/ConfigModel.hpp"
#include "shared/Protocol.hpp"

#include <atomic>
#include <thread>

namespace z3lx::runtime {

// Top-level runtime state machine.
//
// States:
//   Initialising → Running → Stopping → Stopped
//
// All public methods (except Stop) must be called from the runtime thread.
class RuntimeState {
public:
    RuntimeState() noexcept = default;
    ~RuntimeState() noexcept;

    // One-time initialisation.  Returns false on failure.
    [[nodiscard]] bool Initialise(
        const z3lx::shared::ConfigSnapshotPayload& config,
        uint16_t gameVersionMajor,
        uint16_t gameVersionMinor);

    // Start the tick loop on a background thread.
    void Start();

    // Request an ordered shutdown and join the background thread.
    void Stop() noexcept;

    [[nodiscard]] bool IsFpsAvailable() const noexcept;
    [[nodiscard]] bool IsFovAvailable() const noexcept;

private:
    void RunLoop() noexcept;
    void Tick() noexcept;

    z3lx::shared::RuntimeConfig config_ {};
    MemoryResolver::Addresses   addrs_  {};

    FpsService   fps_ {};
    FovService   fov_ {};
    InputSampler input_ {};

    uint16_t gameMajor_ = 0;
    uint16_t gameMinor_ = 0;

    bool fpsAvailable_ = false;
    bool fovAvailable_ = false;

    std::atomic<bool> running_ { false };
    std::thread        thread_ {};
};

} // namespace z3lx::runtime
