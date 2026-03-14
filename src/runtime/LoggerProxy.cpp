#include "runtime/LoggerProxy.hpp"
#include "shared/EventLog.hpp"

#include <string>

namespace z3lx::runtime {

LoggerProxy::LoggerProxy() noexcept = default;
LoggerProxy::~LoggerProxy() noexcept = default;

void LoggerProxy::SetWriter(IpcWriter* value) noexcept {
    writer = value;
}

void LoggerProxy::SetPhase(const std::string_view value) noexcept {
    phase = value;
}

void LoggerProxy::SetForwardingEnabled(const bool enabled) noexcept {
    forwardingEnabled = enabled;
}

void LoggerProxy::Log(const LogLevel level, const std::string_view module,
                      const std::string_view message, const StatusCode code,
                      const uint32_t systemError) noexcept {
    if (!forwardingEnabled || !writer || !writer->IsConnected()) {
        return;
    }

    const std::string moduleString { module };
    const std::string phaseString { phase };
    const std::string messageString { message };

    LogEventMessage logMsg {};
    logMsg.level = static_cast<uint32_t>(level);
    logMsg.code = code;
    logMsg.systemError = systemError;
    CopyToFixedString(logMsg.moduleName, sizeof(logMsg.moduleName),
        moduleString.c_str());
    CopyToFixedString(logMsg.phaseName, sizeof(logMsg.phaseName),
        phaseString.c_str());
    CopyToFixedString(logMsg.message, sizeof(logMsg.message),
        messageString.c_str());
    writer->SendLogEvent(logMsg);

    if (level != LogLevel::Error && level != LogLevel::Fatal) {
        return;
    }

    ErrorEventMessage errorMsg {};
    errorMsg.code = code;
    errorMsg.systemError = systemError;
    CopyToFixedString(errorMsg.moduleName, sizeof(errorMsg.moduleName),
        moduleString.c_str());
    CopyToFixedString(errorMsg.phaseName, sizeof(errorMsg.phaseName),
        phaseString.c_str());
    CopyToFixedString(errorMsg.message, sizeof(errorMsg.message),
        messageString.c_str());
    writer->SendError(errorMsg);
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

void LoggerProxy::Error(const std::string_view message,
                        const StatusCode code,
                        const uint32_t systemError) noexcept {
    Log(LogLevel::Error, "runtime", message, code, systemError);
}

void LoggerProxy::Fatal(const std::string_view message,
                        const StatusCode code,
                        const uint32_t systemError) noexcept {
    Log(LogLevel::Fatal, "runtime", message, code, systemError);
}

} // namespace z3lx::runtime
