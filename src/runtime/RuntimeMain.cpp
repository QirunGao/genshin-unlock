#include "runtime/RuntimeMain.hpp"
#include "runtime/RuntimeState.hpp"
#include "runtime/MemoryResolver.hpp"
#include "runtime/HookManager.hpp"
#include "runtime/FpsService.hpp"
#include "runtime/FovService.hpp"
#include "runtime/InputSampler.hpp"
#include "runtime/IpcWriter.hpp"
#include "runtime/LoggerProxy.hpp"
#include "shared/Protocol.hpp"
#include "shared/VersionTable.hpp"
#include "util/win/Loader.hpp"
#include "util/win/Version.hpp"
#include "util/win/VirtualKey.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <thread>
#include <utility>

#include <Windows.h>

namespace z3lx::runtime {

struct RuntimeContext {
    RuntimeState state;
    MemoryResolver resolver;
    HookManager hookManager;
    FpsService fpsService;
    FovService fovService;
    InputSampler inputSampler;
    IpcWriter ipcWriter;
    LoggerProxy logger;
    ConfigSnapshotMessage config;
    std::chrono::steady_clock::time_point startTime;
};

static void RuntimeLoop(RuntimeContext& ctx);

extern "C" __declspec(dllexport)
RuntimeInitResult RuntimeInitialize(const RuntimeInitParams* params) {
    RuntimeInitResult result {};
    RuntimeContext ctx {};

    ctx.logger.Info("Runtime initializing");
    ctx.startTime = std::chrono::steady_clock::now();

    // Transition: Created -> HostValidated
    ctx.state.TransitionTo(State::HostValidated);

    // Apply configuration from launcher (no file I/O)
    if (params) {
        ctx.config = params->config;
        // Set up IPC writer for heartbeat / hook-state / error events
        if (params->ipcPipeHandle) {
            ctx.ipcWriter.SetHandle(
                static_cast<HANDLE>(params->ipcPipeHandle));
        }
    }

    ctx.state.TransitionTo(State::ConfigReady);

    // Wire HookManager state-change callback to IPC
    ctx.hookManager.SetStateChangeCallback(
        [&ctx](const std::string& name, bool installed, bool enabled) {
            if (!ctx.ipcWriter.IsConnected()) return;
            HookStateChangedMessage msg {};
            CopyToFixedString(msg.hookName, sizeof(msg.hookName),
                name.c_str());
            msg.installed = installed ? 1u : 0u;
            msg.enabled = enabled ? 1u : 0u;
            msg.status = StatusCode::Ok;
            ctx.ipcWriter.SendHookStateChanged(msg);
        });

    // Resolve memory addresses using version table
    const auto versionTable = shared::MakeDefaultVersionTable();

    util::Version gameVersion { 0, 0, 0, 0 };
    try {
        wchar_t exePath[MAX_PATH] {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        const std::filesystem::path gamePath { exePath };
        const std::filesystem::path configIni =
            gamePath.parent_path() / "config.ini";

        if (std::filesystem::exists(configIni)) {
            const HANDLE configFile = CreateFileW(configIni.c_str(),
                GENERIC_READ, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (configFile != INVALID_HANDLE_VALUE) {
                std::string content;
                content.resize(static_cast<size_t>(
                    std::filesystem::file_size(configIni)));
                DWORD bytesRead = 0;
                ReadFile(configFile, content.data(),
                    static_cast<DWORD>(content.size()),
                    &bytesRead, nullptr);
                CloseHandle(configFile);
                content.resize(bytesRead);

                for (auto lineRange :
                     std::views::split(content, '\n')) {
                    std::string_view line { lineRange };
                    auto sep = std::ranges::find(line, '=');
                    if (sep == line.end()) continue;
                    std::string_view key { line.begin(), sep };
                    std::string_view value { sep + 1, line.end() };
                    while (!key.empty() && key.front() == ' ')
                        key.remove_prefix(1);
                    while (!key.empty() && key.back() == ' ')
                        key.remove_suffix(1);
                    while (!value.empty() && value.front() == ' ')
                        value.remove_prefix(1);
                    while (!value.empty() && value.back() == ' ')
                        value.remove_suffix(1);
                    if (key == "game_version") {
                        gameVersion = util::Version { value };
                        break;
                    }
                }
            }
        }
    } catch (...) {
        ctx.logger.Warn("Failed to read game version from config.ini");
    }

    const auto resolveStatus = ctx.resolver.Resolve(versionTable, gameVersion);
    if (resolveStatus == StatusCode::Ok) {
        ctx.state.TransitionTo(State::SymbolsResolved);
    } else {
        ctx.logger.Error("Failed to resolve memory addresses");
        result.status = resolveStatus;
        ctx.state.TransitionTo(State::Degraded);
        return result;
    }

    const auto addresses = ctx.resolver.GetAddresses();
    if (!addresses) {
        result.status = StatusCode::SymbolResolveFailed;
        ctx.state.TransitionTo(State::Degraded);
        return result;
    }

    // Initialize FPS service via HookManager
    if (addresses->fpsAddress) {
        ctx.hookManager.Register(HookDefinition {
            .name = "FpsUnlock",
            .target = addresses->fpsAddress,
            .installFn = [&ctx, addr = addresses->fpsAddress]() {
                return ctx.fpsService.Initialize(addr) == StatusCode::Ok;
            },
            .setEnabledFn = [&ctx](bool e) {
                ctx.fpsService.SetEnabled(e);
            }
        });
    }

    // Initialize FOV service via HookManager
    if (addresses->fovTarget) {
        ctx.hookManager.Register(HookDefinition {
            .name = "FovUnlock",
            .target = addresses->fovTarget,
            .installFn = [&ctx, target = addresses->fovTarget]() {
                return ctx.fovService.Initialize(target) == StatusCode::Ok;
            },
            .setEnabledFn = [&ctx](bool e) {
                ctx.fovService.SetEnabled(e);
            }
        });
    }

    // HookManager installs all registered hooks
    ctx.hookManager.InstallAll();
    result.fpsAvailable = ctx.hookManager.IsInstalled("FpsUnlock");
    result.fovAvailable = ctx.hookManager.IsInstalled("FovUnlock");

    ctx.state.TransitionTo(State::HooksInstalled);

    // Apply initial config from snapshot
    ctx.fpsService.SetEnabled(ctx.config.unlockFps != 0);
    ctx.fpsService.SetTargetFps(ctx.config.targetFps);
    ctx.fpsService.SetAutoThrottle(ctx.config.autoThrottle != 0);
    ctx.fovService.SetEnabled(ctx.config.unlockFov != 0);
    ctx.fovService.SetTargetFov(ctx.config.targetFov);
    ctx.fovService.SetSmoothing(ctx.config.fovSmoothing);

    // Enable all hooks via HookManager
    ctx.hookManager.EnableAll();

    ctx.state.TransitionTo(State::Running);
    ctx.logger.Info("Runtime initialized successfully");

    result.status = StatusCode::Ok;

    // Launch runtime loop in background thread
    std::thread([ctx = std::move(ctx)]() mutable {
        RuntimeLoop(ctx);
    }).detach();

    return result;
}

static void RuntimeLoop(RuntimeContext& ctx) {
    using VK = util::VirtualKey;
    const auto unlockFovKey = static_cast<VK>(ctx.config.unlockFovKey);
    const auto nextPresetKey = static_cast<VK>(ctx.config.nextFovPresetKey);
    const auto prevPresetKey = static_cast<VK>(ctx.config.prevFovPresetKey);

    uint32_t tickCount = 0;
    // Clamp preset count to array bounds
    const auto presetCount = std::min(
        ctx.config.fovPresetCount,
        static_cast<uint32_t>(shared::kMaxFovPresets));

    while (!ctx.state.IsTerminal()) {
        ctx.inputSampler.Sample();

        // Apply FPS config (continuous from snapshot)
        ctx.fpsService.Update();

        // Handle FOV key bindings (in-memory only, no file I/O)
        if (ctx.inputSampler.IsKeyDown(unlockFovKey)) {
            const bool nowEnabled = !(ctx.config.unlockFov != 0);
            ctx.config.unlockFov = nowEnabled ? 1u : 0u;
            ctx.fovService.SetEnabled(nowEnabled);
        } else if (ctx.config.unlockFov != 0) {
            if (ctx.inputSampler.IsKeyDown(nextPresetKey) &&
                presetCount > 0) {
                const auto count = static_cast<int32_t>(presetCount);
                int32_t nextFov = ctx.config.fovPresets[0];
                for (int32_t i = 0; i < count; ++i) {
                    if (ctx.config.fovPresets[i] > ctx.config.targetFov) {
                        nextFov = ctx.config.fovPresets[i];
                        break;
                    }
                }
                ctx.config.targetFov = nextFov;
                ctx.fovService.SetTargetFov(nextFov);
            } else if (ctx.inputSampler.IsKeyDown(prevPresetKey) &&
                       presetCount > 0) {
                const auto count = static_cast<int32_t>(presetCount);
                int32_t prevFov = ctx.config.fovPresets[count - 1];
                for (int32_t i = count - 1; i >= 0; --i) {
                    if (ctx.config.fovPresets[i] < ctx.config.targetFov) {
                        prevFov = ctx.config.fovPresets[i];
                        break;
                    }
                }
                ctx.config.targetFov = prevFov;
                ctx.fovService.SetTargetFov(prevFov);
            }
        }

        ctx.fovService.Update();

        // Heartbeat at ~1 Hz (every 60 ticks at 60 Hz)
        ++tickCount;
        if (tickCount >= 60 && ctx.ipcWriter.IsConnected()) {
            tickCount = 0;
            const auto now = std::chrono::steady_clock::now();
            const auto uptime = std::chrono::duration_cast<
                std::chrono::seconds>(now - ctx.startTime);

            StatusHeartbeatMessage heartbeat {};
            heartbeat.runtimeState =
                static_cast<uint32_t>(ctx.state.GetState());
            heartbeat.fpsActive =
                ctx.hookManager.IsEnabled("FpsUnlock") ? 1u : 0u;
            heartbeat.fovActive =
                ctx.hookManager.IsEnabled("FovUnlock") ? 1u : 0u;
            heartbeat.uptimeSeconds =
                static_cast<uint32_t>(uptime.count());
            ctx.ipcWriter.SendHeartbeat(heartbeat);
        }

        // Tick at ~60 Hz
        std::this_thread::sleep_for(
            std::chrono::duration<double, std::milli>(1000.0 / 60));
    }
}

} // namespace z3lx::runtime
