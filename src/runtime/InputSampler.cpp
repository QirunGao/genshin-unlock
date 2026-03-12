#include "runtime/InputSampler.hpp"

#include <Windows.h>

namespace z3lx::runtime {

void InputSampler::Sample() noexcept {
    for (size_t i = 0; i < KEY_COUNT; ++i) {
        previous_[i] = current_[i];
        current_[i]  = (GetAsyncKeyState(static_cast<int>(i)) & 0x8000) != 0;
    }
}

bool InputSampler::IsJustPressed(
    const util::VirtualKey key) const noexcept
{
    const auto idx = static_cast<size_t>(static_cast<uint8_t>(key));
    return current_[idx] && !previous_[idx];
}

} // namespace z3lx::runtime
