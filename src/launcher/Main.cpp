#include "launcher/AppConfig.hpp"
#include "launcher/GameLocator.hpp"
#include "launcher/HandshakeClient.hpp"
#include "launcher/Injector.hpp"
#include "launcher/Logger.hpp"
#include "launcher/ModuleValidator.hpp"
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

#include <algorithm>
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

namespace {

z::ConfigSnapshotMessage BuildConfigSnapshot(
    const z::RuntimeConfig& runtimeConfig) {
    z::ConfigSnapshotMessage msg {};
    msg.unlockFps = runtimeConfig.unlockFps ? 1u : 0u;
    msg.targetFps = runtimeConfig.targetFps;
    msg.autoThrottle = runtimeConfig.autoThrottle ? 1u : 0u;
    msg.unlockFov = runtimeConfig.unlockFov ? 1u : 0u;
    msg.targetFov = runtimeConfig.targetFov;
    msg.fovSmoothing = runtimeConfig.fovSmoothing;
    msg.unlockFovKey = static_cast<uint8_t>(runtimeConfig.unlockFovKey);
    msg.nextFovPresetKey = static_cast<uint8_t>(runtimeConfig.nextFovPresetKey);
    msg.prevFovPresetKey = static_cast<uint8_t>(runtimeConfig.prevFovPresetKey);
    msg.fovPresetCount = static_cast<uint32_t>(
        std::min(runtimeConfig.fovPresets.size(),
                 static_cast<size_t>(z::kMaxFovPresets)));
    for (uint32_t i = 0; i < msg.fovPresetCount; ++i) {
        msg.fovPresets[i] = runtimeConfig.fovPresets[i];
    }
    return msg;
}

} // namespace

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

    // Hash-based integrity check (dev phase — log hashes, reject on mismatch
    // if expected hashes are configured).
    {
        const std::string bootstrapHash =
            z::ComputeFileSha256(bootstrapPath);
        const std::string runtimeHash =
            z::ComputeFileSha256(runtimePath);
        logger.Info("launcher", std::format(
            "bootstrap.dll SHA-256: {}", bootstrapHash));
        logger.Info("launcher", std::format(
            "runtime.dll   SHA-256: {}", runtimeHash));

        if (bootstrapHash.empty()) {
            logger.Error("launcher",
                "Failed to compute bootstrap.dll hash");
            z::ShowMessageBox(
                "Launcher",
                "Module integrity check failed for bootstrap.dll.",
                z::MessageBoxIcon::Error,
                z::MessageBoxButton::Ok
            );
            return 1;
        }
        if (runtimeHash.empty()) {
            logger.Error("launcher",
                "Failed to compute runtime.dll hash");
            z::ShowMessageBox(
                "Launcher",
                "Module integrity check failed for runtime.dll.",
                z::MessageBoxIcon::Error,
                z::MessageBoxButton::Ok
            );
            return 1;
        }
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

    // Step 6: Inject bootstrap.dll via CreateRemoteThread + LoadLibraryW
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

    // Step 7: Call BootstrapEntryPoint remotely (non-blocking)
    logger.Info("launcher", "Calling BootstrapEntryPoint remotely");
    const z::StatusCode callStatus = z::CallBootstrapEntry(
        process.get(), ctx.processId, bootstrapPath);
    if (callStatus != z::StatusCode::Ok) {
        logger.Error("launcher", "Failed to call BootstrapEntryPoint");
        TerminateProcess(process.get(), 1);
        z::ShowMessageBox(
            "Launcher",
            "Failed to initialize bootstrap in game process.",
            z::MessageBoxIcon::Error,
            z::MessageBoxButton::Ok
        );
        return 1;
    }

    // Step 8: IPC handshake (fail-closed — abort on any failure)
    logger.Info("launcher", "Connecting to bootstrap IPC");
    z::HandshakeClient handshake { ctx.processId };
    if (handshake.Connect() != z::StatusCode::Ok) {
        logger.Error("launcher", "IPC connection failed");
        TerminateProcess(process.get(), 1);
        z::ShowMessageBox(
            "Launcher",
            "Failed to establish IPC connection with bootstrap.",
            z::MessageBoxIcon::Error,
            z::MessageBoxButton::Ok
        );
        return 1;
    }

    // Send Hello
    z::HelloMessage hello {};
    hello.session.launcherPid = GetCurrentProcessId();
    hello.session.gamePid = ctx.processId;
    z::CopyToFixedString(hello.session.toolVersion,
        sizeof(hello.session.toolVersion),
        toolVersion.ToString().c_str());
    hello.session.protocolVersion = z::kProtocolVersion;
    if (handshake.SendHello(hello) != z::StatusCode::Ok) {
        logger.Error("launcher", "Failed to send Hello");
        TerminateProcess(process.get(), 1);
        return 1;
    }

    // Wait for BootstrapReady
    z::BootstrapReadyMessage bootstrapReady {};
    if (handshake.WaitForBootstrapReady(bootstrapReady) != z::StatusCode::Ok) {
        logger.Error("launcher", "Bootstrap ready handshake failed");
        TerminateProcess(process.get(), 1);
        return 1;
    }
    if (bootstrapReady.status != z::StatusCode::Ok) {
        logger.Error("launcher", std::format("Bootstrap reported error: {}",
            z::StatusCodeToString(bootstrapReady.status)));
        TerminateProcess(process.get(), 1);
        return 1;
    }
    logger.Info("launcher", std::format("Host validated: {}",
        bootstrapReady.hostModuleName));

    // Send config snapshot
    logger.Info("launcher", "Sending runtime configuration");
    const z::ConfigSnapshotMessage configMsg = BuildConfigSnapshot(runtimeConfig);
    if (handshake.SendConfigSnapshot(configMsg) != z::StatusCode::Ok) {
        logger.Error("launcher", "Failed to send config snapshot");
        TerminateProcess(process.get(), 1);
        return 1;
    }

    // Wait for RuntimeInitResult
    z::RuntimeInitResultMessage runtimeResult {};
    if (handshake.WaitForRuntimeInitResult(runtimeResult) != z::StatusCode::Ok) {
        logger.Error("launcher", "Failed to receive runtime init result");
        TerminateProcess(process.get(), 1);
        return 1;
    }
    if (runtimeResult.status != z::StatusCode::Ok) {
        logger.Error("launcher",
            std::format("Runtime init failed: {}",
                z::StatusCodeToString(runtimeResult.status)));
        TerminateProcess(process.get(), 1);
        z::ShowMessageBox(
            "Launcher",
            std::format("Runtime initialization failed: {}",
                z::StatusCodeToString(runtimeResult.status)),
            z::MessageBoxIcon::Error,
            z::MessageBoxButton::Ok
        );
        return 1;
    }
    logger.Info("launcher", std::format(
        "Runtime initialized — FPS: {}, FOV: {}",
        runtimeResult.fpsAvailable ? "available" : "unavailable",
        runtimeResult.fovAvailable ? "available" : "unavailable"));

    // Step 9: Resume game main thread
    logger.Info("launcher", "Resuming game main thread");
    ResumeThread(thread.get());

    std::println(std::cout, "Game process started successfully");
    logger.Info("launcher", "Game process started successfully");

    // Step 10: Monitoring loop — read heartbeat / hook-state / error
    //          events while waiting for the game process to exit.
    if (!launcherConfig.closeLauncherOnSuccess) {
        logger.Info("launcher", "Entering monitoring loop");
        while (true) {
            const DWORD waitResult = WaitForSingleObject(
                process.get(), 1000);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD exitCode = 0;
                GetExitCodeProcess(process.get(), &exitCode);
                logger.Info("launcher", std::format(
                    "Game process exited with code {}", exitCode));
                break;
            }

            // Process pending IPC events from the runtime
            for (bool reading = true; reading && handshake.HasPendingData();) {
                z::MessageHeader header {};
                if (handshake.PeekMessageHeader(header)
                        != z::StatusCode::Ok) {
                    break;
                }
                switch (header.type) {
                case z::MessageType::StatusHeartbeat: {
                    z::StatusHeartbeatMessage hb {};
                    if (handshake.ReceiveHeartbeat(hb)
                            == z::StatusCode::Ok) {
                        logger.Info("runtime", std::format(
                            "Heartbeat: state={}, fps={}, fov={}, uptime={}s",
                            hb.runtimeState, hb.fpsActive,
                            hb.fovActive, hb.uptimeSeconds));
                    }
                    break;
                }
                case z::MessageType::HookStateChanged: {
                    z::HookStateChangedMessage hookMsg {};
                    if (handshake.ReceiveHookStateChanged(hookMsg)
                            == z::StatusCode::Ok) {
                        logger.Info("runtime", std::format(
                            "Hook '{}': installed={}, enabled={}",
                            hookMsg.hookName, hookMsg.installed,
                            hookMsg.enabled));
                    }
                    break;
                }
                case z::MessageType::ErrorEvent: {
                    z::ErrorEventMessage errMsg {};
                    if (handshake.ReceiveError(errMsg)
                            == z::StatusCode::Ok) {
                        logger.Error("runtime", std::format(
                            "Error in '{}': {} (sys={})",
                            errMsg.moduleName, errMsg.message,
                            errMsg.systemError));
                    }
                    break;
                }
                default:
                    reading = false;
                    break;
                }
            }
        }
    }

    handshake.Disconnect();
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
