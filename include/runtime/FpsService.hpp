#pragma once

#include "shared/StatusCode.hpp"

#include <cstdint>

namespace z3lx::runtime {

using namespace z3lx::shared;

class FpsService {
public:
    FpsService() noexcept;
    ~FpsService() noexcept;

    StatusCode Initialize(int* targetFpsAddress);
    void Shutdown() noexcept;

    void SetEnabled(bool enabled) noexcept;
    void SetTargetFps(int fps) noexcept;
    void SetAutoThrottle(bool enabled) noexcept;

    void Update() noexcept;

    [[nodiscard]] bool IsEnabled() const noexcept;
    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] int GetTargetFps() const noexcept;

private:
    bool available = false;
    bool enabled = false;
    bool autoThrottle = false;
    int targetFps = 60;
    int* targetFpsPtr = nullptr;

    [[nodiscard]] int ApplyThrottling(int requestedFps) const noexcept;
};

} // namespace z3lx::runtime
