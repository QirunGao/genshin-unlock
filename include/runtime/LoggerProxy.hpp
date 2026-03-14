#pragma once

#include "shared/EventLog.hpp"
#include "runtime/IpcWriter.hpp"

#include <string_view>

namespace z3lx::runtime {

using namespace z3lx::shared;

class LoggerProxy {
public:
    LoggerProxy() noexcept;
    ~LoggerProxy() noexcept;

    void SetWriter(IpcWriter* writer) noexcept;
    void SetPhase(std::string_view phase) noexcept;
    void SetForwardingEnabled(bool enabled) noexcept;

    void Log(LogLevel level, std::string_view module,
             std::string_view message,
             StatusCode code = StatusCode::Ok,
             uint32_t systemError = 0) noexcept;

    void Trace(std::string_view message) noexcept;
    void Debug(std::string_view message) noexcept;
    void Info(std::string_view message) noexcept;
    void Warn(std::string_view message) noexcept;
    void Error(std::string_view message,
               StatusCode code = StatusCode::Ok,
               uint32_t systemError = 0) noexcept;
    void Fatal(std::string_view message,
               StatusCode code = StatusCode::Ok,
               uint32_t systemError = 0) noexcept;

private:
    IpcWriter* writer = nullptr;
    std::string_view phase = "runtime";
    bool forwardingEnabled = false;
};

} // namespace z3lx::runtime
