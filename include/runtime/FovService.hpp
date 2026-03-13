#pragma once

#include "shared/StatusCode.hpp"
#include "util/ExponentialFilter.hpp"

#include <cstdint>
#include <memory>
#include <mutex>

namespace z3lx::runtime {

using namespace z3lx::shared;

class FovService {
public:
    FovService() noexcept;
    ~FovService() noexcept;

    FovService(FovService&& other) noexcept;
    FovService& operator=(FovService&& other) noexcept;
    FovService(const FovService&) = delete;
    FovService& operator=(const FovService&) = delete;

    StatusCode Initialize(void* fovTarget);

    void SetEnabled(bool enabled) noexcept;
    void SetTargetFov(int fov) noexcept;
    void SetSmoothing(float smoothing) noexcept;

    void Update() noexcept;

    [[nodiscard]] bool IsEnabled() const noexcept;
    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] bool IsHooked() const noexcept;
    [[nodiscard]] int GetTargetFov() const noexcept;

    // Called from the hook callback — not part of the public API
    void HandleHookCallback(void* instance, float& value) noexcept;

private:
    bool available = false;
    bool enabled = false;
    bool hooked = false;
    bool enabledOnce = false;
    int targetFov = 45;

    int setFovCount = 0;
    void* previousInstance = nullptr;
    float previousFov = 45.0f;
    bool isPreviousFov = false;

    util::ExponentialFilter<float> filter {};
    std::unique_ptr<std::mutex> mutex;
};

} // namespace z3lx::runtime
