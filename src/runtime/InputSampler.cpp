#include "runtime/InputSampler.hpp"
#include "util/win/VirtualKey.hpp"

#include <cstdint>

#include <Windows.h>

namespace z3lx::runtime {

InputSampler::InputSampler() noexcept = default;
InputSampler::~InputSampler() noexcept = default;

void InputSampler::Sample() noexcept {
    previousKeyStates = currentKeyStates;
    for (size_t i = 0; i < kKeyCount; ++i) {
        const auto keyIndex = static_cast<uint8_t>(i);
        const bool isKeyDown = GetAsyncKeyState(keyIndex) & 0x8000;
        currentKeyStates[keyIndex] = isKeyDown;
    }
}

bool InputSampler::IsKeyDown(const util::VirtualKey key) const noexcept {
    const auto keyIndex = static_cast<uint8_t>(key);
    return currentKeyStates[keyIndex] && !previousKeyStates[keyIndex];
}

bool InputSampler::IsKeyHeld(const util::VirtualKey key) const noexcept {
    const auto keyIndex = static_cast<uint8_t>(key);
    return currentKeyStates[keyIndex];
}

bool InputSampler::IsKeyUp(const util::VirtualKey key) const noexcept {
    const auto keyIndex = static_cast<uint8_t>(key);
    return !currentKeyStates[keyIndex] && previousKeyStates[keyIndex];
}

} // namespace z3lx::runtime
