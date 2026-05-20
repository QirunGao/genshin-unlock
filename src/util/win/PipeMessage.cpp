#include "util/win/PipeMessage.hpp"

#include <wil/result.h>

#include <cstdint>

#include <Windows.h>

namespace z3lx::util {
std::vector<uint8_t> ReadPipeMessage(const HANDLE pipe) {
    uint32_t size = 0;
    DWORD bytesRead = 0;
    THROW_IF_WIN32_BOOL_FALSE(ReadFile(
        pipe,
        &size,
        sizeof(size),
        &bytesRead,
        nullptr
    ));

    std::vector<uint8_t> buffer(size);
    if (!buffer.empty()) {
        THROW_IF_WIN32_BOOL_FALSE(ReadFile(
            pipe,
            buffer.data(),
            size,
            &bytesRead,
            nullptr
        ));
    }
    return buffer;
}

void WritePipeMessage(
    const HANDLE pipe,
    const std::vector<uint8_t>& buffer) {
    const uint32_t size = static_cast<uint32_t>(buffer.size());
    DWORD bytesWritten = 0;
    THROW_IF_WIN32_BOOL_FALSE(WriteFile(
        pipe,
        &size,
        sizeof(size),
        &bytesWritten,
        nullptr
    ));
    if (!buffer.empty()) {
        THROW_IF_WIN32_BOOL_FALSE(WriteFile(
            pipe,
            buffer.data(),
            size,
            &bytesWritten,
            nullptr
        ));
    }
}
} // namespace z3lx::util
