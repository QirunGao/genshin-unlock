#pragma once

#include "shared/EventLog.hpp"

#include <filesystem>
#include <string_view>

namespace z3lx::launcher {

using namespace z3lx::shared;

class Logger {
public:
    explicit Logger(LogLevel minLevel = LogLevel::Info) noexcept;
    ~Logger() noexcept;

    void SetLogFile(const std::filesystem::path& logFilePath);

    void Log(LogLevel level, std::string_view module,
             std::string_view phase, std::string_view message) noexcept;

    void Trace(std::string_view module, std::string_view message) noexcept;
    void Debug(std::string_view module, std::string_view message) noexcept;
    void Info(std::string_view module, std::string_view message) noexcept;
    void Warn(std::string_view module, std::string_view message) noexcept;
    void Error(std::string_view module, std::string_view message) noexcept;
    void Fatal(std::string_view module, std::string_view message) noexcept;

    void LogRemoteEvent(const LogEvent& event) noexcept;

private:
    LogLevel minLevel;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
};

} // namespace z3lx::launcher
