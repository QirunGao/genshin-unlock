#pragma once

#include "StatusCode.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace z3lx::shared {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

struct LogEvent {
    LogLevel level = LogLevel::Info;
    std::string moduleName;
    std::string phaseName;
    StatusCode code = StatusCode::Ok;
    uint32_t systemError = 0;
    std::string message;
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();
};

constexpr std::string_view LogLevelToString(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:   return "TRACE";
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    case LogLevel::Fatal:   return "FATAL";
    default:                return "UNKNOWN";
    }
}

} // namespace z3lx::shared
