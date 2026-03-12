#include "runtime/RuntimeState.hpp"
#include "runtime/MemoryResolver.hpp"
#include "shared/ConfigModel.hpp"
#include "shared/Protocol.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include <Windows.h>

namespace z3lx::runtime {

RuntimeState::~RuntimeState() noexcept {
    Stop();
}

bool RuntimeState::Initialise(
    const z3lx::shared::ConfigSnapshotPayload& snap,
    const uint16_t gameVersionMajor,
    const uint16_t gameVersionMinor)
{
    gameMajor_ = gameVersionMajor;
    gameMinor_ = gameVersionMinor;

    // Convert ConfigSnapshotPayload → shared::RuntimeConfig
    config_ = {};
    config_.unlockFps    = snap.unlockFps;
    config_.targetFps    = snap.targetFps;
    config_.autoThrottle = snap.autoThrottle;
    config_.unlockFov    = snap.unlockFov;
    config_.targetFov    = snap.targetFov;
    config_.fovSmoothing = snap.fovSmoothing;

    const int count = (std::min)(snap.fovPresetCount,
        z3lx::shared::CONFIG_MAX_FOV_PRESETS);
    config_.fovPresets.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        config_.fovPresets[static_cast<size_t>(i)] = snap.fovPresets[i];
    }

    config_.unlockFovKey     = static_cast<util::VirtualKey>(snap.unlockFovKey);
    config_.nextFovPresetKey =
        static_cast<util::VirtualKey>(snap.nextFovPresetKey);
    config_.prevFovPresetKey =
        static_cast<util::VirtualKey>(snap.prevFovPresetKey);

    // Resolve game memory addresses from version whitelist
    const auto resolved = MemoryResolver::Resolve(gameMajor_, gameMinor_);
    if (!resolved) {
        return false;
    }
    addrs_ = *resolved;

    // Initialise services
    if (addrs_.fpsTarget) {
        fps_.SetTarget(addrs_.fpsTarget);
        fps_.ApplyConfig(config_);
        fpsAvailable_ = true;
    }

    if (addrs_.fovFunc) {
        try {
            fov_.SetTarget(addrs_.fovFunc);
            fov_.ApplyConfig(config_);
            fovAvailable_ = true;
        } catch (...) {
            fovAvailable_ = false;
        }
    }

    return true;
}

void RuntimeState::Start() {
    running_ = true;
    thread_  = std::thread { [this] { RunLoop(); } };
}

void RuntimeState::Stop() noexcept {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool RuntimeState::IsFpsAvailable() const noexcept { return fpsAvailable_; }
bool RuntimeState::IsFovAvailable() const noexcept { return fovAvailable_; }

void RuntimeState::RunLoop() noexcept {
    using Milliseconds = std::chrono::duration<double, std::milli>;
    constexpr uint16_t TICK_RATE = 60;
    const Milliseconds tickInterval { 1000.0 / TICK_RATE };

    while (running_) {
        Tick();
        std::this_thread::sleep_for(tickInterval);
    }
}

void RuntimeState::Tick() noexcept {
    // Sample input
    input_.Sample();

    // Determine window / cursor state
    const HWND foreground   = GetForegroundWindow();
    bool windowFocused = false;
    if (foreground) {
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        windowFocused = (pid == GetCurrentProcessId());
    }

    CURSORINFO ci { .cbSize = sizeof(ci) };
    const bool cursorVisible =
        GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);

    // Tick FPS service
    fps_.Tick(windowFocused);

    // Control FOV hook activation based on focus and cursor visibility
    const bool fovShouldBeActive = windowFocused && !cursorVisible;
    fov_.SetHookActive(fovShouldBeActive);

    // FOV preset cycling
    if (fovAvailable_ && config_.unlockFov) {
        const bool nextPressed = input_.IsJustPressed(config_.nextFovPresetKey);
        const bool prevPressed = input_.IsJustPressed(config_.prevFovPresetKey);

        if (nextPressed || prevPressed) {
            const int presetCount = static_cast<int>(config_.fovPresets.size());
            if (presetCount > 0) {
                auto it = std::ranges::find(config_.fovPresets, config_.targetFov);
                const int idx = (it != config_.fovPresets.end())
                    ? static_cast<int>(it - config_.fovPresets.begin())
                    : 0;

                if (nextPressed) {
                    config_.targetFov =
                        config_.fovPresets[
                            static_cast<size_t>((idx + 1) % presetCount)];
                } else {
                    config_.targetFov =
                        config_.fovPresets[
                            static_cast<size_t>(
                                (idx - 1 + presetCount) % presetCount)];
                }
                fov_.ApplyConfig(config_);
            }
        }

        if (input_.IsJustPressed(config_.unlockFovKey)) {
            config_.unlockFov = !config_.unlockFov;
            fov_.ApplyConfig(config_);
        }
    }
}

} // namespace z3lx::runtime
