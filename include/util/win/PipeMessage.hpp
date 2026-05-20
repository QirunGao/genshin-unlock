#pragma once

#include <cstdint>
#include <vector>

#include <Windows.h>

namespace z3lx::util {
std::vector<uint8_t> ReadPipeMessage(HANDLE pipe);

void WritePipeMessage(
    HANDLE pipe,
    const std::vector<uint8_t>& buffer
);
} // namespace z3lx::util
