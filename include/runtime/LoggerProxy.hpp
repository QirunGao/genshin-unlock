#pragma once

#include "shared/EventLog.hpp"

#include <string_view>

namespace z3lx::runtime {

using namespace z3lx::shared;

class LoggerProxy {
public:
    LoggerProxy() noexcept;
    ~LoggerProxy() noexcept;

    void Log(LogLevel level, std::string_view module,
             std::string_view message) noexcept;

    void Trace(std::string_view message) noexcept;
    void Debug(std::string_view message) noexcept;
    void Info(std::string_view message) noexcept;
    void Warn(std::string_view message) noexcept;
    void Error(std::string_view message) noexcept;
    void Fatal(std::string_view message) noexcept;
};

} // namespace z3lx::runtime
