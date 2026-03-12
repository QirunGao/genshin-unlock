#pragma once

#include "shared/ConfigModel.hpp"
#include "util/win/VirtualKey.hpp"

#include <cstdint>

namespace z3lx::runtime {

// Polls keyboard state each tick.
// Only reports a transition (down→held, held→up) to avoid repeated events.
class InputSampler {
public:
    InputSampler() noexcept = default;

    // Snapshot key states – call once per runtime tick.
    void Sample() noexcept;

    // Returns true only on the tick the key was first pressed.
    [[nodiscard]] bool IsJustPressed(util::VirtualKey key) const noexcept;

private:
    static constexpr size_t KEY_COUNT = 256;
    bool current_[KEY_COUNT]  = {};
    bool previous_[KEY_COUNT] = {};
};

} // namespace z3lx::runtime
