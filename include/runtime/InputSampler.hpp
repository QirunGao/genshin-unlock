#pragma once

#include "util/win/VirtualKey.hpp"

#include <bitset>
#include <cstdint>

namespace z3lx::runtime {

class InputSampler {
public:
    InputSampler() noexcept;
    ~InputSampler() noexcept;

    void Sample() noexcept;

    [[nodiscard]] bool IsKeyDown(util::VirtualKey key) const noexcept;
    [[nodiscard]] bool IsKeyHeld(util::VirtualKey key) const noexcept;
    [[nodiscard]] bool IsKeyUp(util::VirtualKey key) const noexcept;

private:
    static constexpr auto kKeyCount = 256;
    std::bitset<kKeyCount> currentKeyStates;
    std::bitset<kKeyCount> previousKeyStates;
};

} // namespace z3lx::runtime
