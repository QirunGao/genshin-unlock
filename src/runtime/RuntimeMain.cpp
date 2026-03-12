#include "runtime/RuntimeMain.hpp"
#include "runtime/RuntimeState.hpp"
#include "runtime/MemoryResolver.hpp"
#include "runtime/HookManager.hpp"
#include "runtime/FpsService.hpp"
#include "runtime/FovService.hpp"
#include "runtime/InputSampler.hpp"
#include "runtime/LoggerProxy.hpp"
#include "shared/ConfigModel.hpp"
#include "shared/Protocol.hpp"
#include "shared/VersionTable.hpp"
#include "util/win/Loader.hpp"
#include "util/win/Version.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

#include <Windows.h>

namespace z3lx::runtime {

// File-scope persistent config for hot-reload
static RuntimeConfig s_runtimeConfig {};
static std::filesystem::path s_configFilePath {};
static bool s_configDirty = false;

struct RuntimeContext {
    RuntimeState state;
    MemoryResolver resolver;
    HookManager hookManager;
    FpsService fpsService;
    FovService fovService;
    InputSampler inputSampler;
    LoggerProxy logger;
    ConfigSnapshotMessage configSnapshot;
};

static void RuntimeLoop(RuntimeContext& ctx);

extern "C" __declspec(dllexport)
RuntimeInitResult RuntimeInitialize(const RuntimeInitParams* params) {
    RuntimeInitResult result {};
    RuntimeContext ctx {};

    ctx.logger.Info("Runtime initializing");

    // Transition: Created -> HostValidated
    ctx.state.TransitionTo(State::HostValidated);

    // Resolve addresses using version table
    const auto versionTable = shared::MakeDefaultVersionTable();

    // Read game version from config.ini near the game exe
    const std::filesystem::path currentPath =
        util::GetCurrentModuleFilePath().parent_path();

    util::Version gameVersion { 0, 0, 0, 0 };
    try {
        // Try to find the game executable
        wchar_t exePath[MAX_PATH] {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        const std::filesystem::path gamePath { exePath };
        const std::filesystem::path configIni =
            gamePath.parent_path() / "config.ini";

        if (std::filesystem::exists(configIni)) {
            const wil::unique_hfile configFile =
                wil::open_or_create_file(configIni.c_str());
            std::string content;
            content.resize(static_cast<size_t>(
                std::filesystem::file_size(configIni)));
            DWORD bytesRead = 0;
            ReadFile(configFile.get(), content.data(),
                static_cast<DWORD>(content.size()), &bytesRead, nullptr);
            content.resize(bytesRead);

            for (auto lineRange : std::views::split(content, '\n')) {
                std::string_view line { lineRange };
                auto sep = std::ranges::find(line, '=');
                if (sep == line.end()) continue;
                std::string_view key { line.begin(), sep };
                std::string_view value { sep + 1, line.end() };
                // trim whitespace
                while (!key.empty() && key.front() == ' ') key.remove_prefix(1);
                while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
                while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
                while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
                if (key == "game_version") {
                    gameVersion = util::Version { value };
                    break;
                }
            }
        }
    } catch (...) {
        ctx.logger.Warn("Failed to read game version");
    }

    ctx.state.TransitionTo(State::ConfigReady);

    // Resolve memory addresses
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

    // Initialize FPS service
    if (addresses->fpsAddress) {
        const auto fpsStatus = ctx.fpsService.Initialize(addresses->fpsAddress);
        result.fpsAvailable = (fpsStatus == StatusCode::Ok);
    }

    // Initialize FOV service
    if (addresses->fovTarget) {
        const auto fovStatus = ctx.fovService.Initialize(addresses->fovTarget);
        result.fovAvailable = (fovStatus == StatusCode::Ok);
    }

    ctx.state.TransitionTo(State::HooksInstalled);

    // Apply initial config if provided
    if (params) {
        ctx.configSnapshot = params->config;
        ctx.fpsService.SetEnabled(ctx.configSnapshot.unlockFps);
        ctx.fpsService.SetTargetFps(ctx.configSnapshot.targetFps);
        ctx.fpsService.SetAutoThrottle(ctx.configSnapshot.autoThrottle);
        ctx.fovService.SetEnabled(ctx.configSnapshot.unlockFov);
        ctx.fovService.SetTargetFov(ctx.configSnapshot.targetFov);
        ctx.fovService.SetSmoothing(ctx.configSnapshot.fovSmoothing);
    }

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
    // Read persistent config file for hot-reload
    const std::filesystem::path currentPath =
        util::GetCurrentModuleFilePath().parent_path();
    s_configFilePath = currentPath / "runtime_config.json";

    try {
        std::vector<uint8_t> buffer;
        if (std::filesystem::exists(s_configFilePath)) {
            const wil::unique_hfile configFile = wil::open_or_create_file(
                s_configFilePath.c_str());
            LARGE_INTEGER fileSize {};
            GetFileSizeEx(configFile.get(), &fileSize);
            buffer.resize(static_cast<size_t>(fileSize.QuadPart));
            DWORD bytesRead = 0;
            ReadFile(configFile.get(), buffer.data(),
                static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
            buffer.resize(bytesRead);
            s_runtimeConfig.Deserialize(buffer);
        }
    } catch (...) {}

    while (!ctx.state.IsTerminal()) {
        // Sample input
        ctx.inputSampler.Sample();

        // Apply runtime config
        ctx.fpsService.SetEnabled(s_runtimeConfig.unlockFps);
        ctx.fpsService.SetTargetFps(s_runtimeConfig.targetFps);
        ctx.fpsService.SetAutoThrottle(s_runtimeConfig.autoThrottle);

        // Handle FOV key bindings
        if (ctx.inputSampler.IsKeyDown(s_runtimeConfig.unlockFovKey)) {
            s_runtimeConfig.unlockFov = !s_runtimeConfig.unlockFov;
            s_configDirty = true;
        } else if (s_runtimeConfig.unlockFov) {
            if (ctx.inputSampler.IsKeyDown(s_runtimeConfig.nextFovPresetKey)) {
                const auto& presets = s_runtimeConfig.fovPresets;
                const auto it = std::ranges::find_if(
                    presets,
                    [&](const int p) { return s_runtimeConfig.targetFov < p; }
                );
                s_runtimeConfig.targetFov =
                    (it != presets.end()) ? *it : presets.front();
                s_configDirty = true;
            } else if (ctx.inputSampler.IsKeyDown(s_runtimeConfig.prevFovPresetKey)) {
                const auto& presets = s_runtimeConfig.fovPresets;
                const auto it = std::ranges::find_if(
                    presets | std::views::reverse,
                    [&](const int p) { return s_runtimeConfig.targetFov > p; }
                );
                s_runtimeConfig.targetFov =
                    (it != presets.rend()) ? *it : presets.back();
                s_configDirty = true;
            }
        }

        ctx.fovService.SetEnabled(s_runtimeConfig.unlockFov);
        ctx.fovService.SetTargetFov(s_runtimeConfig.targetFov);
        ctx.fovService.SetSmoothing(s_runtimeConfig.fovSmoothing);

        // Update services
        ctx.fpsService.Update();
        ctx.fovService.Update();

        // Save config if changed
        if (s_configDirty) {
            s_configDirty = false;
            try {
                std::vector<uint8_t> buffer;
                s_runtimeConfig.Serialize(buffer);
                const wil::unique_hfile configFile = wil::open_or_create_file(
                    s_configFilePath.c_str());
                SetFilePointerEx(configFile.get(), {}, nullptr, FILE_BEGIN);
                SetEndOfFile(configFile.get());
                DWORD written = 0;
                WriteFile(configFile.get(), buffer.data(),
                    static_cast<DWORD>(buffer.size()), &written, nullptr);
            } catch (...) {}
        }

        // Tick at 60Hz
        using Milliseconds = std::chrono::duration<double, std::milli>;
        const Milliseconds duration { 1000.0 / 60 };
        std::this_thread::sleep_for(duration);
    }
}

} // namespace z3lx::runtime
