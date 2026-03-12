#include "launcher/ProcessStarter.hpp"
#include "common/Constants.hpp"
#include "util/win/String.hpp"

#include <wil/result.h>

#include <cstdint>
#include <format>
#include <string>

#include <Windows.h>

namespace z3lx::launcher {

ProcessContext StartGameProcess(const LauncherConfig& config) {
    std::wstring args = [&config] {
        if (!config.overrideArgs) {
            return std::wstring {};
        }

        const wchar_t* modeArgs = [](const DisplayMode mode) {
            switch (mode) {
            case DisplayMode::Windowed:
                return L"-screen-fullscreen 0";
            case DisplayMode::Fullscreen:
                return L"-screen-fullscreen 1 -window-mode exclusive";
            case DisplayMode::Borderless:
                return L"-popupwindow -screen-fullscreen 0";
            default:
                return L"";
            }
        }(config.displayMode);

        const wchar_t* mobileArgs = config.mobilePlatform
            ? L"use_mobile_platform -is_cloud 1 "
              "-platform_type CLOUD_THIRD_PARTY_MOBILE"
            : L"";

        std::wstring additionalArgs {};
        util::U8ToU16(config.additionalArgs, additionalArgs);

        return std::format(
            L"-monitor {} {} -screen-width {} -screen-height {} {} {} ",
            config.monitorIndex,
            modeArgs,
            config.screenWidth,
            config.screenHeight,
            mobileArgs,
            additionalArgs
        );
    }();

    STARTUPINFOW si { .cb = sizeof(si) };
    PROCESS_INFORMATION pi {};
    THROW_IF_WIN32_BOOL_FALSE(CreateProcessW(
        config.gamePath.c_str(),
        args.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED,
        nullptr,
        config.gamePath.parent_path().c_str(),
        &si,
        &pi
    ));

    return ProcessContext {
        .processHandle = pi.hProcess,
        .threadHandle = pi.hThread,
        .processId = pi.dwProcessId,
        .threadId = pi.dwThreadId
    };
}

} // namespace z3lx::launcher
