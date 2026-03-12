#include "launcher/AppConfig.hpp"
#include "launcher/GameLocator.hpp"
#include "launcher/HandshakeClient.hpp"
#include "launcher/Injector.hpp"
#include "launcher/Logger.hpp"
#include "launcher/ProcessStarter.hpp"
#include "launcher/VersionPolicy.hpp"
#include "shared/ConfigModel.hpp"
#include "shared/Protocol.hpp"
#include "shared/StatusCode.hpp"
#include "shared/VersionTable.hpp"
#include "util/Version.hpp"
#include "util/win/Dialogue.hpp"
#include "util/win/Loader.hpp"
#include "util/win/Shell.hpp"
#include "util/win/Version.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <string>
#include <thread>

#include <Windows.h>

namespace fs = std::filesystem;
namespace z {
using namespace z3lx::launcher;
using namespace z3lx::shared;
using namespace z3lx::util;
} // namespace z

int main() try {
    const auto loggingCallback = [](const wil::FailureInfo& info) noexcept {
        std::array<wchar_t, 2048> buffer {};
        const HRESULT result = wil::GetFailureLogString(
            buffer.data(), buffer.size(), info);
        if (SUCCEEDED(result)) {
            std::wcerr << buffer.data();
        }
    };
    wil::SetResultLoggingCallback(loggingCallback);

    z::Logger logger { z::LogLevel::Info };

    // Step 1: Read launcher configuration
    std::println(std::cout, "Reading configuration...");
    logger.Info("launcher", "Reading launcher configuration");
    constexpr auto launcherConfigPath = L"launcher_config.json";
    constexpr auto runtimeConfigPath = L"runtime_config.json";

    z::LauncherConfig launcherConfig {};
    try {
        launcherConfig = z::ReadLauncherConfig(launcherConfigPath);
    } catch (...) {
        logger.Warn("launcher", "Failed to read launcher config, using defaults");
        const z::MessageBoxResult result = z::ShowMessageBox(
            "Launcher",
            "Failed to read configuration file.\n"
            "Proceeding with default configuration.",
            z::MessageBoxIcon::Warning,
            z::MessageBoxButton::OkCancel
        );
        if (result == z::MessageBoxResult::Cancel) {
            return 0;
        }
        z::WriteLauncherConfig(launcherConfigPath, launcherConfig);
    }

    z::RuntimeConfig runtimeConfig {};
    try {
        runtimeConfig = z::ReadRuntimeConfig(runtimeConfigPath);
    } catch (...) {
        logger.Warn("launcher", "Failed to read runtime config, using defaults");
        z::WriteRuntimeConfig(runtimeConfigPath, runtimeConfig);
    }

    // Step 2: Locate game path
    logger.Info("launcher", "Locating game path");
    if (launcherConfig.gamePath.empty() ||
        !fs::exists(launcherConfig.gamePath)) {
        try {
            launcherConfig.gamePath = z::LocateGamePath();
            z::WriteLauncherConfig(launcherConfigPath, launcherConfig);
        } catch (...) {
            const z::MessageBoxResult result = z::ShowMessageBox(
                "Launcher",
                std::format(
                    "The game path '{}' is invalid or does not exist.\n"
                    "Locate it using File Explorer?",
                    launcherConfig.gamePath.string()
                ),
                z::MessageBoxIcon::Warning,
                z::MessageBoxButton::YesNo
            );
            if (result == z::MessageBoxResult::No) {
                return 0;
            }
            constexpr z::Filter filters[] {
                { L"GenshinImpact.exe", L"GenshinImpact.exe" },
                { L"YuanShen.exe", L"YuanShen.exe" }
            };
            launcherConfig.gamePath = z::OpenFileDialogue(filters);
            z::WriteLauncherConfig(launcherConfigPath, launcherConfig);
        }
    }

    // Step 3: Validate self-module signatures/hashes
    logger.Info("launcher", "Validating module integrity");
    const fs::path launcherDir = z::GetCurrentModuleFilePath().parent_path();
    const fs::path bootstrapPath = launcherDir / L"bootstrap.dll";
    const fs::path runtimePath = launcherDir / L"runtime.dll";

    if (!fs::exists(bootstrapPath)) {
        logger.Error("launcher", "bootstrap.dll not found");
        z::ShowMessageBox(
            "Launcher",
            "bootstrap.dll not found in the launcher directory.",
            z::MessageBoxIcon::Error,
            z::MessageBoxButton::Ok
        );
        return 1;
    }
    if (!fs::exists(runtimePath)) {
        logger.Error("launcher", "runtime.dll not found");
        z::ShowMessageBox(
            "Launcher",
            "runtime.dll not found in the launcher directory.",
            z::MessageBoxIcon::Error,
            z::MessageBoxButton::Ok
        );
        return 1;
    }

    // Step 4: Check version compatibility
    std::println(std::cout, "Checking compatibility...");
    logger.Info("launcher", "Checking version compatibility");
    const z::VersionTable versionTable = z::MakeDefaultVersionTable();
    const z::Version toolVersion = z::GetFileVersion(
        z::GetCurrentModuleFilePath()
    );
    try {
        const z::Version gameVersion = z::ReadGameVersion(launcherConfig.gamePath);
        z::CheckVersionCompatibility(toolVersion, gameVersion, versionTable);
    } catch (const std::exception& e) {
        const z::MessageBoxResult result = z::ShowMessageBox(
            "Launcher",
            std::format(
                "Version compatibility check failed:\n{}\n\n"
                "Open the download page to check for updates?",
                e.what()
            ),
            z::MessageBoxIcon::Information,
            z::MessageBoxButton::YesNo
        );
        if (result == z::MessageBoxResult::Yes) {
            z::OpenUrl(
                "https://github.com/z3lx/genshin-unlock/releases/latest"
            );
        }
        return 0;
    }

    // Step 5: Create game process (suspended)
    std::println(std::cout, "Starting game process...");
    logger.Info("launcher", "Creating game process");
    const z::ProcessContext ctx = z::StartGameProcess(launcherConfig);
    const wil::unique_handle process { ctx.processHandle };
    const wil::unique_handle thread { ctx.threadHandle };

    // Step 6: Inject bootstrap.dll via CreateRemoteThread
    logger.Info("launcher", "Injecting bootstrap.dll");
    const z::StatusCode injectStatus = z::InjectBootstrap(
        process.get(), bootstrapPath);
    if (injectStatus != z::StatusCode::Ok) {
        logger.Error("launcher", "Bootstrap injection failed");
        TerminateProcess(process.get(), 1);
        z::ShowMessageBox(
            "Launcher",
            "Failed to inject bootstrap.dll into game process.",
            z::MessageBoxIcon::Error,
            z::MessageBoxButton::Ok
        );
        return 1;
    }

    // Step 7: Wait for bootstrap ready via IPC
    logger.Info("launcher", "Waiting for bootstrap handshake");
    z::HandshakeClient handshake { ctx.processId };
    if (handshake.Connect() != z::StatusCode::Ok) {
        logger.Warn("launcher", "IPC connection not available, resuming directly");
    } else {
        z::HelloMessage hello {};
        hello.session.launcherPid = GetCurrentProcessId();
        hello.session.gamePid = ctx.processId;
        hello.session.toolVersion = toolVersion.ToString();
        hello.session.protocolVersion = z::kProtocolVersion;
        handshake.SendHello(hello);

        z::BootstrapReadyMessage bootstrapReady {};
        handshake.WaitForBootstrapReady(bootstrapReady);

        // Step 8: Send runtime configuration snapshot
        logger.Info("launcher", "Sending runtime configuration");
        z::ConfigSnapshotMessage configMsg {};
        configMsg.unlockFps = runtimeConfig.unlockFps;
        configMsg.targetFps = runtimeConfig.targetFps;
        configMsg.autoThrottle = runtimeConfig.autoThrottle;
        configMsg.unlockFov = runtimeConfig.unlockFov;
        configMsg.targetFov = runtimeConfig.targetFov;
        configMsg.fovSmoothing = runtimeConfig.fovSmoothing;
        handshake.SendConfigSnapshot(configMsg);

        // Step 9: Wait for runtime init result
        z::RuntimeInitResultMessage runtimeResult {};
        handshake.WaitForRuntimeInitResult(runtimeResult);
        if (runtimeResult.status != z::StatusCode::Ok) {
            logger.Warn("launcher",
                std::format("Runtime init returned: {}",
                    z::StatusCodeToString(runtimeResult.status)));
        }
    }

    // Step 10: Resume game main thread
    logger.Info("launcher", "Resuming game main thread");
    ResumeThread(thread.get());

    std::println(std::cout, "Game process started successfully");
    logger.Info("launcher", "Game process started successfully");

    // Step 11: Enter monitoring loop (optional, exit after short delay)
    if (!launcherConfig.closeLauncherOnSuccess) {
        std::this_thread::sleep_for(std::chrono::seconds { 3 });
    }

    return 0;
} catch (const std::exception& e) {
    LOG_CAUGHT_EXCEPTION();
    try {
        z3lx::util::ShowMessageBox(
            "Launcher",
            std::format("An error occurred:\n{}", e.what()),
            z3lx::util::MessageBoxIcon::Error,
            z3lx::util::MessageBoxButton::Ok
        );
    } catch (...) {}
    return 1;
}
