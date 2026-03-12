#include "runtime/LoggerProxy.hpp"
#include "shared/EventLog.hpp"

namespace z3lx::runtime {

LoggerProxy::LoggerProxy() noexcept = default;
LoggerProxy::~LoggerProxy() noexcept = default;

void LoggerProxy::Log(const LogLevel level, const std::string_view module,
                       const std::string_view message) noexcept {
    // In the future, this will relay logs via IPC to the launcher.
    // For now, this is a placeholder.
    (void)level;
    (void)module;
    (void)message;
}

void LoggerProxy::Trace(const std::string_view message) noexcept {
    Log(LogLevel::Trace, "runtime", message);
}

void LoggerProxy::Debug(const std::string_view message) noexcept {
    Log(LogLevel::Debug, "runtime", message);
}

void LoggerProxy::Info(const std::string_view message) noexcept {
    Log(LogLevel::Info, "runtime", message);
}

void LoggerProxy::Warn(const std::string_view message) noexcept {
    Log(LogLevel::Warning, "runtime", message);
}

void LoggerProxy::Error(const std::string_view message) noexcept {
    Log(LogLevel::Error, "runtime", message);
}

void LoggerProxy::Fatal(const std::string_view message) noexcept {
    Log(LogLevel::Fatal, "runtime", message);
}

} // namespace z3lx::runtime
