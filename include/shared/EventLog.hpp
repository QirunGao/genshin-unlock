#pragma once

#include "shared/StatusCode.hpp"

#include <cstdint>

namespace z3lx::shared {

enum class LogLevel : uint8_t {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3,
};

struct LogEvent {
    LogLevel   level;
    char       module[64];
    char       phase[64];
    StatusCode code;
    char       message[512];
};

} // namespace z3lx::shared
