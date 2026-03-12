#include "launcher/Logger.hpp"
#include "util/win/File.hpp"
#include "util/win/String.hpp"

#include <wil/filesystem.h>
#include <wil/resource.h>

#include <chrono>
#include <format>
#include <iostream>
#include <print>
#include <string>

#include <Windows.h>

namespace z3lx::launcher {

Logger::Logger(const LogLevel minLevel) noexcept
    : minLevel { minLevel } {}

Logger::~Logger() noexcept {
    if (fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(fileHandle);
    }
}

void Logger::SetLogFile(const std::filesystem::path& logFilePath) {
    const wil::unique_hfile handle = wil::open_or_truncate_existing_file(
        logFilePath.native().c_str());
    fileHandle = handle.release();
}

void Logger::Log(const LogLevel level, const std::string_view module,
                 const std::string_view phase, const std::string_view message) noexcept {
    if (level < minLevel) return;

    try {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);

        std::string logLine = std::format("[{}] [{}] [{}] {}\n",
            LogLevelToString(level), module, phase, message);

        std::print(std::cout, "{}", logLine);

        if (fileHandle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(fileHandle, logLine.data(),
                static_cast<DWORD>(logLine.size()), &written, nullptr);
        }
    } catch (...) {}
}

void Logger::Trace(const std::string_view module, const std::string_view message) noexcept {
    Log(LogLevel::Trace, module, "", message);
}

void Logger::Debug(const std::string_view module, const std::string_view message) noexcept {
    Log(LogLevel::Debug, module, "", message);
}

void Logger::Info(const std::string_view module, const std::string_view message) noexcept {
    Log(LogLevel::Info, module, "", message);
}

void Logger::Warn(const std::string_view module, const std::string_view message) noexcept {
    Log(LogLevel::Warning, module, "", message);
}

void Logger::Error(const std::string_view module, const std::string_view message) noexcept {
    Log(LogLevel::Error, module, "", message);
}

void Logger::Fatal(const std::string_view module, const std::string_view message) noexcept {
    Log(LogLevel::Fatal, module, "", message);
}

void Logger::LogRemoteEvent(const LogEvent& event) noexcept {
    Log(event.level, event.moduleName, event.phaseName, event.message);
}

} // namespace z3lx::launcher
