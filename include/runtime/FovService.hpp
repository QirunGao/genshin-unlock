#pragma once

#include "shared/StatusCode.hpp"
#include "util/ExponentialFilter.hpp"

#include <cstdint>
#include <mutex>

namespace z3lx::runtime {

using namespace z3lx::shared;

struct FovRuntimeState {
    int targetFov = 45;
    bool enabled = false;
    bool enabledOnce = false;
    bool hooked = false;

    int setFovCount = 0;
    void* previousInstance = nullptr;
    float previousFov = 45.0f;
    bool isPreviousFov = false;

    util::ExponentialFilter<float> filter {};
};

class FovService {
public:
    FovService() noexcept;
    ~FovService() noexcept;

    StatusCode Initialize(void* fovTarget);

    void SetEnabled(bool enabled) noexcept;
    void SetTargetFov(int fov) noexcept;
    void SetSmoothing(float smoothing) noexcept;
    void SetHooked(bool hooked) noexcept;

    void Update() noexcept;

    [[nodiscard]] bool IsEnabled() const noexcept;
    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] bool IsHooked() const noexcept;
    [[nodiscard]] int GetTargetFov() const noexcept;

private:
    bool available = false;
    std::mutex mutex;
    FovRuntimeState state;
};

} // namespace z3lx::runtime
