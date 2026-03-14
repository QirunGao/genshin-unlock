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
#include "util/Version.hpp"
#include "util/win/VirtualKey.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Windows.h>

namespace z3lx::runtime {

namespace {

constexpr std::string_view kRuntimeInitPhase = "runtime_init";
constexpr std::string_view kRuntimeLoopPhase = "runtime_loop";
constexpr std::string_view kStateTransitionPhase = "state_transition";
constexpr std::string_view kControlPlanePhase = "control_plane_start";
constexpr std::string_view kFatalPhase = "fatal";

struct PendingStateChange {
    State previousState = State::Created;
    State currentState = State::Created;
    std::array<char, kMaxStringLen> phaseName {};
};

} // namespace

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
    std::atomic<bool> controlPlaneReady = false;
    bool startupDegraded = false;
    std::atomic<bool> shutdownRequested = false;
    std::vector<PendingStateChange> pendingStateChanges;
};

static std::shared_ptr<RuntimeContext> g_runtimeContext;
static std::mutex g_runtimeContextMutex;

static bool TransitionTo(RuntimeContext& ctx, State state) noexcept;
static void QueuePendingStateChange(RuntimeContext& ctx, State previousState,
    State currentState, std::string_view phase) noexcept;
static void FlushPendingStateChanges(RuntimeContext& ctx) noexcept;
static void ApplyInitialConfig(RuntimeContext& ctx) noexcept;
static void ApplyConfiguredHookStates(RuntimeContext& ctx) noexcept;
static StatusCode EmitCurrentState(RuntimeContext& ctx, State previousState,
    State currentState, std::string_view phase) noexcept;
static void EmitHeartbeat(RuntimeContext& ctx) noexcept;
static void EmitHookState(RuntimeContext& ctx, std::string_view hookName) noexcept;
static void ControlLoop(std::shared_ptr<RuntimeContext> ctx);
static void HeartbeatLoop(std::shared_ptr<RuntimeContext> ctx);
static void HandleControlMessages(RuntimeContext& ctx) noexcept;
static void HandleInput(RuntimeContext& ctx,
    util::VirtualKey unlockFovKey,
    util::VirtualKey nextPresetKey,
    util::VirtualKey prevPresetKey,
    uint32_t presetCount) noexcept;
static void RuntimeLoop(RuntimeContext& ctx);
static RuntimeInitResult MakeInitFailure(const RuntimeContext* ctx,
    StatusCode code, std::string_view phase, std::string_view message,
    uint32_t systemError = 0) noexcept;
static ControlPlaneStartResult MakeControlPlaneResult(
    StatusCode code, State state, std::string_view phase,
    std::string_view message, uint32_t systemError = 0) noexcept;
static void SignalFatal(RuntimeContext& ctx, StatusCode code,
    std::string_view phase, std::string_view message,
    uint32_t systemError = 0) noexcept;

extern "C" __declspec(dllexport)
RuntimeInitResult RuntimeInitialize(const RuntimeInitParams* params) {
    auto ctx = std::make_shared<RuntimeContext>();
    RuntimeContext& runtime = *ctx;

    runtime.logger.SetWriter(&runtime.ipcWriter);
    runtime.logger.SetPhase(kRuntimeInitPhase);
    runtime.startTime = std::chrono::steady_clock::now();

    if (!params) {
        return MakeInitFailure(nullptr, StatusCode::ConfigInvalid,
            kRuntimeInitPhase, "Runtime init params were not provided.");
    }

    runtime.config = params->config;
    if (params->gamePid != GetCurrentProcessId()) {
        return MakeInitFailure(&runtime, StatusCode::ProtocolMismatch,
            kRuntimeInitPhase,
            "Runtime init params contain a mismatched game PID.");
    }
    if (params->gameVersion[0] == '\0') {
        return MakeInitFailure(&runtime, StatusCode::ProtocolMismatch,
            kRuntimeInitPhase,
            "Runtime init params are missing the game version.");
    }
    if (!params->ipcPipeHandle) {
        return MakeInitFailure(&runtime, StatusCode::IpcDisconnected,
            kRuntimeInitPhase, "IPC pipe handle is missing.");
    }
    runtime.ipcWriter.SetHandle(static_cast<HANDLE>(params->ipcPipeHandle));
    if (!runtime.ipcWriter.IsConnected()) {
        return MakeInitFailure(&runtime, StatusCode::IpcDisconnected,
            kRuntimeInitPhase, "IPC pipe handle is not connected.");
    }

    TransitionTo(runtime, State::HostValidated);
    TransitionTo(runtime, State::IpcReady);
    TransitionTo(runtime, State::ConfigReady);

    // Wire HookManager state-change callback to IPC
    runtime.hookManager.SetStateChangeCallback(
        [&runtime](const std::string& name, const bool installed,
                   const bool enabled) {
            if (!runtime.controlPlaneReady.load() ||
                !runtime.ipcWriter.IsConnected()) {
                return;
            }
            HookStateChangedMessage msg {};
            CopyToFixedString(msg.hookName, sizeof(msg.hookName),
                name.c_str());
            msg.installed = installed ? 1u : 0u;
            msg.enabled = enabled ? 1u : 0u;
            msg.status = installed ? StatusCode::Ok
                                   : StatusCode::HookInstallFailed;
            (void)runtime.ipcWriter.SendHookStateChanged(msg);
        });

    const auto versionTable = shared::MakeDefaultVersionTable();
    try {
        const util::Version gameVersion {
            std::string_view { params->gameVersion }
        };
        const auto resolveStatus = runtime.resolver.Resolve(versionTable, gameVersion);
        if (resolveStatus == StatusCode::Ok) {
            TransitionTo(runtime, State::SymbolsResolved);
        } else {
            TransitionTo(runtime, State::Degraded);
            return MakeInitFailure(&runtime, resolveStatus,
                "symbol_resolve", "Failed to resolve game symbols.");
        }
    } catch (...) {
        TransitionTo(runtime, State::Degraded);
        return MakeInitFailure(&runtime, StatusCode::GameVersionUnsupported,
            "symbol_resolve", "Game version is unsupported.");
    }

    const auto addresses = runtime.resolver.GetAddresses();
    if (!addresses) {
        TransitionTo(runtime, State::Degraded);
        return MakeInitFailure(&runtime, StatusCode::SymbolResolveFailed,
            "symbol_resolve", "Resolved address table is empty.");
    }

    if (addresses->fpsAddress) {
        runtime.hookManager.Register(HookDefinition {
            .name = "FpsUnlock",
            .target = addresses->fpsAddress,
            .installFn = [&runtime, addr = addresses->fpsAddress]() {
                return runtime.fpsService.Initialize(addr) == StatusCode::Ok;
            },
            .uninstallFn = [&runtime]() {
                runtime.fpsService.Shutdown();
            },
            .setEnabledFn = [&runtime](const bool enabled) {
                runtime.fpsService.SetEnabled(enabled);
            }
        });
    }

    if (addresses->fovTarget) {
        runtime.hookManager.Register(HookDefinition {
            .name = "FovUnlock",
            .target = addresses->fovTarget,
            .installFn = [&runtime, target = addresses->fovTarget]() {
                return runtime.fovService.Initialize(target) == StatusCode::Ok;
            },
            .uninstallFn = [&runtime]() {
                runtime.fovService.Shutdown();
            },
            .setEnabledFn = [&runtime](const bool enabled) {
                runtime.fovService.SetEnabled(enabled);
            }
        });
    }

    const StatusCode hookInstallStatus = runtime.hookManager.InstallAll();
    RuntimeInitResult result {};
    result.fpsAvailable = runtime.hookManager.IsInstalled("FpsUnlock");
    result.fovAvailable = runtime.hookManager.IsInstalled("FovUnlock");

    if (hookInstallStatus != StatusCode::Ok ||
        (!result.fpsAvailable && !result.fovAvailable)) {
        TransitionTo(runtime, State::Degraded);
        return MakeInitFailure(&runtime, StatusCode::HookInstallFailed,
            "hook_install", "No runtime hooks were installed successfully.");
    }
    runtime.startupDegraded = !(result.fpsAvailable && result.fovAvailable);

    TransitionTo(runtime, State::HooksInstalled);

    ApplyInitialConfig(runtime);
    ApplyConfiguredHookStates(runtime);

    if (runtime.startupDegraded) {
        TransitionTo(runtime, State::Degraded);
    } else {
        TransitionTo(runtime, State::Running);
    }
    result.status = StatusCode::Ok;
    result.runtimeState = static_cast<uint32_t>(runtime.state.GetState());
    {
        std::lock_guard lock { g_runtimeContextMutex };
        g_runtimeContext = ctx;
    }

    return result;
}

extern "C" __declspec(dllexport)
ControlPlaneStartResult RuntimeStartControlPlane() {
    std::shared_ptr<RuntimeContext> ctx;
    {
        std::lock_guard lock { g_runtimeContextMutex };
        ctx = g_runtimeContext;
    }
    if (!ctx) {
        return MakeControlPlaneResult(StatusCode::RuntimeInitFailed,
            State::Created, kControlPlanePhase,
            "Runtime context is not initialized.");
    }
    if (ctx->controlPlaneReady.load()) {
        return MakeControlPlaneResult(StatusCode::Ok, ctx->state.GetState(),
            kControlPlanePhase, "Runtime control plane is already running.");
    }

    ControlPlaneReadyMessage readyMsg {};
    readyMsg.status = StatusCode::Ok;
    readyMsg.runtimeState = static_cast<uint32_t>(ctx->state.GetState());
    CopyToFixedString(readyMsg.phaseName, sizeof(readyMsg.phaseName),
        std::string { kControlPlanePhase }.c_str());
    CopyToFixedString(readyMsg.message, sizeof(readyMsg.message),
        "Runtime control plane is ready.");
    if (ctx->ipcWriter.SendControlPlaneReady(readyMsg) != StatusCode::Ok) {
        return MakeControlPlaneResult(StatusCode::IpcDisconnected,
            ctx->state.GetState(), kControlPlanePhase,
            "Failed to send control-plane readiness.");
    }

    ctx->controlPlaneReady.store(true);
    ctx->logger.SetPhase(kRuntimeLoopPhase);
    ctx->logger.SetForwardingEnabled(true);
    FlushPendingStateChanges(*ctx);
    EmitHookState(*ctx, "FpsUnlock");
    EmitHookState(*ctx, "FovUnlock");
    if (ctx->startupDegraded) {
        ctx->logger.Warn("Runtime started with reduced capability.");
    } else {
        ctx->logger.Info("Runtime loop started");
    }

    std::thread([ctx]() {
        try {
            HeartbeatLoop(ctx);
        } catch (...) {
            SignalFatal(*ctx, StatusCode::RuntimeInitFailed, "heartbeat_loop",
                "Unhandled exception in heartbeat loop.");
        }
    }).detach();
    std::thread([ctx]() {
        try {
            ControlLoop(ctx);
        } catch (...) {
            SignalFatal(*ctx, StatusCode::RuntimeInitFailed, "control_loop",
                "Unhandled exception in control loop.");
        }
    }).detach();
    std::thread([ctx]() {
        try {
            RuntimeLoop(*ctx);
        } catch (...) {
            SignalFatal(*ctx, StatusCode::RuntimeInitFailed, "runtime_loop",
                "Unhandled exception in runtime loop.");
        }
        std::lock_guard lock { g_runtimeContextMutex };
        if (g_runtimeContext == ctx) {
            g_runtimeContext.reset();
        }
    }).detach();
    return MakeControlPlaneResult(StatusCode::Ok, ctx->state.GetState(),
        kControlPlanePhase, "Runtime control plane started.");
}

static bool TransitionTo(RuntimeContext& ctx, const State state) noexcept {
    const State previousState = ctx.state.GetState();
    if (!ctx.state.TransitionTo(state)) {
        return false;
    }
    if (ctx.controlPlaneReady.load()) {
        (void)EmitCurrentState(
            ctx, previousState, state, kStateTransitionPhase);
    } else {
        QueuePendingStateChange(
            ctx, previousState, state, kStateTransitionPhase);
    }
    return true;
}

static void QueuePendingStateChange(RuntimeContext& ctx,
    const State previousState, const State currentState,
    const std::string_view phase) noexcept {
    PendingStateChange event {};
    event.previousState = previousState;
    event.currentState = currentState;
    CopyToFixedString(event.phaseName.data(), event.phaseName.size(),
        std::string { phase }.c_str());
    ctx.pendingStateChanges.push_back(event);
}

static void FlushPendingStateChanges(RuntimeContext& ctx) noexcept {
    for (const auto& event : ctx.pendingStateChanges) {
        (void)EmitCurrentState(ctx, event.previousState,
            event.currentState, event.phaseName.data());
    }
    ctx.pendingStateChanges.clear();
}

static void ApplyInitialConfig(RuntimeContext& ctx) noexcept {
    ctx.fpsService.SetTargetFps(ctx.config.targetFps);
    ctx.fpsService.SetAutoThrottle(ctx.config.autoThrottle != 0);
    ctx.fovService.SetTargetFov(ctx.config.targetFov);
    ctx.fovService.SetSmoothing(ctx.config.fovSmoothing);
}

static void ApplyConfiguredHookStates(RuntimeContext& ctx) noexcept {
    if (ctx.hookManager.IsInstalled("FpsUnlock")) {
        if (ctx.config.unlockFps != 0) {
            ctx.hookManager.Enable("FpsUnlock");
        } else {
            ctx.hookManager.Disable("FpsUnlock");
        }
    }
    if (ctx.hookManager.IsInstalled("FovUnlock")) {
        if (ctx.config.unlockFov != 0) {
            ctx.hookManager.Enable("FovUnlock");
        } else {
            ctx.hookManager.Disable("FovUnlock");
        }
    }
}

static StatusCode EmitCurrentState(RuntimeContext& ctx, const State previousState,
    const State currentState, const std::string_view phase) noexcept {
    if (!ctx.ipcWriter.IsConnected()) {
        return StatusCode::IpcDisconnected;
    }

    StateChangedMessage msg {};
    msg.previousState = static_cast<uint32_t>(previousState);
    msg.currentState = static_cast<uint32_t>(currentState);
    msg.status = StatusCode::Ok;
    CopyToFixedString(msg.phaseName, sizeof(msg.phaseName),
        std::string { phase }.c_str());
    return ctx.ipcWriter.SendStateChanged(msg);
}

static void EmitHeartbeat(RuntimeContext& ctx) noexcept {
    if (!ctx.ipcWriter.IsConnected()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto uptime = std::chrono::duration_cast<
        std::chrono::seconds>(now - ctx.startTime);

    StatusHeartbeatMessage heartbeat {};
    heartbeat.runtimeState =
        static_cast<uint32_t>(ctx.state.GetState());
    heartbeat.fpsActive =
        (ctx.fpsService.IsAvailable() && ctx.fpsService.IsEnabled()) ? 1u : 0u;
    heartbeat.fovActive =
        (ctx.fovService.IsAvailable() && ctx.fovService.IsEnabled() &&
            ctx.fovService.IsHooked()) ? 1u : 0u;
    heartbeat.uptimeSeconds =
        static_cast<uint32_t>(uptime.count());
    (void)ctx.ipcWriter.SendHeartbeat(heartbeat);
}

static void EmitHookState(
    RuntimeContext& ctx, const std::string_view hookName) noexcept {
    if (!ctx.ipcWriter.IsConnected()) {
        return;
    }

    const std::string hookNameString { hookName };
    HookStateChangedMessage msg {};
    CopyToFixedString(msg.hookName, sizeof(msg.hookName),
        hookNameString.c_str());
    msg.installed = ctx.hookManager.IsInstalled(hookNameString) ? 1u : 0u;
    if (hookName == "FpsUnlock") {
        msg.enabled = ctx.fpsService.IsEnabled() ? 1u : 0u;
    } else if (hookName == "FovUnlock") {
        msg.enabled = ctx.fovService.IsEnabled() ? 1u : 0u;
    } else {
        msg.enabled = ctx.hookManager.IsEnabled(hookNameString) ? 1u : 0u;
    }
    msg.status = msg.installed ? StatusCode::Ok : StatusCode::HookInstallFailed;
    (void)ctx.ipcWriter.SendHookStateChanged(msg);
}

static void ControlLoop(std::shared_ptr<RuntimeContext> ctx) {
    while (!ctx->state.IsTerminal()) {
        HandleControlMessages(*ctx);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void HeartbeatLoop(std::shared_ptr<RuntimeContext> ctx) {
    while (!ctx->state.IsTerminal()) {
        EmitHeartbeat(*ctx);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

static void HandleControlMessages(RuntimeContext& ctx) noexcept {
    if (!ctx.ipcWriter.HasPendingData()) {
        return;
    }

    MessageHeader header {};
    if (ctx.ipcWriter.PeekMessageHeader(header) != StatusCode::Ok) {
        return;
    }

    if (header.type != MessageType::ShutdownRequest) {
        return;
    }

    ShutdownRequestMessage shutdown {};
    if (ctx.ipcWriter.ReceiveShutdown(shutdown) != StatusCode::Ok) {
        return;
    }

    (void)shutdown;
    ctx.shutdownRequested.store(true);
}

static void HandleInput(RuntimeContext& ctx,
    const util::VirtualKey unlockFovKey,
    const util::VirtualKey nextPresetKey,
    const util::VirtualKey prevPresetKey,
    const uint32_t presetCount) noexcept {
    if (ctx.inputSampler.IsKeyDown(unlockFovKey)) {
        const bool nowEnabled = !(ctx.config.unlockFov != 0);
        ctx.config.unlockFov = nowEnabled ? 1u : 0u;
        if (ctx.hookManager.IsInstalled("FovUnlock")) {
            if (nowEnabled) {
                ctx.hookManager.Enable("FovUnlock");
            } else {
                ctx.hookManager.Disable("FovUnlock");
            }
        }
        return;
    }

    if (ctx.config.unlockFov == 0 || presetCount == 0) {
        return;
    }

    if (ctx.inputSampler.IsKeyDown(nextPresetKey)) {
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
        return;
    }

    if (ctx.inputSampler.IsKeyDown(prevPresetKey)) {
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

static void RuntimeLoop(RuntimeContext& ctx) {
    using VK = util::VirtualKey;
    const auto unlockFovKey = static_cast<VK>(ctx.config.unlockFovKey);
    const auto nextPresetKey = static_cast<VK>(ctx.config.nextFovPresetKey);
    const auto prevPresetKey = static_cast<VK>(ctx.config.prevFovPresetKey);

    const auto presetCount = (std::min)(
        ctx.config.fovPresetCount,
        static_cast<uint32_t>(shared::kMaxFovPresets));

    while (!ctx.state.IsTerminal()) {
        if (ctx.shutdownRequested.exchange(false)) {
            ctx.logger.Info("Received shutdown request");
            ctx.hookManager.DisableAll();
            ctx.hookManager.UninstallAll();
            TransitionTo(ctx, State::Shutdown);
            break;
        }
        ctx.inputSampler.Sample();
        HandleInput(ctx, unlockFovKey, nextPresetKey, prevPresetKey, presetCount);
        if (ctx.state.GetState() != State::Fatal) {
            ctx.fpsService.Update();
            ctx.fovService.Update();
        }

        std::this_thread::sleep_for(
            std::chrono::duration<double, std::milli>(1000.0 / 60));
    }
}

static RuntimeInitResult MakeInitFailure(const RuntimeContext* ctx,
    const StatusCode code, const std::string_view phase,
    const std::string_view message, const uint32_t systemError) noexcept {
    RuntimeInitResult result {};
    result.status = code;
    result.runtimeState = ctx
        ? static_cast<uint32_t>(ctx->state.GetState())
        : static_cast<uint32_t>(State::Created);
    result.systemError = systemError;
    CopyToFixedString(result.phaseName, sizeof(result.phaseName),
        std::string { phase }.c_str());
    CopyToFixedString(result.message, sizeof(result.message),
        std::string { message }.c_str());
    return result;
}

static ControlPlaneStartResult MakeControlPlaneResult(
    const StatusCode code, const State state, const std::string_view phase,
    const std::string_view message, const uint32_t systemError) noexcept {
    ControlPlaneStartResult result {};
    result.status = code;
    result.runtimeState = static_cast<uint32_t>(state);
    result.systemError = systemError;
    CopyToFixedString(result.phaseName, sizeof(result.phaseName),
        std::string { phase }.c_str());
    CopyToFixedString(result.message, sizeof(result.message),
        std::string { message }.c_str());
    return result;
}

static void SignalFatal(RuntimeContext& ctx, const StatusCode code,
    const std::string_view phase, const std::string_view message,
    const uint32_t systemError) noexcept {
    if (ctx.state.IsTerminal()) {
        return;
    }

    const State previousState = ctx.state.GetState();
    if (!ctx.state.TransitionTo(State::Fatal)) {
        return;
    }

    ctx.logger.SetPhase(phase);
    ctx.logger.Fatal(message, code, systemError);
    ctx.hookManager.DisableAll();
    ctx.hookManager.UninstallAll();

    if (ctx.controlPlaneReady.load()) {
        (void)EmitCurrentState(ctx, previousState, State::Fatal, kFatalPhase);
    }
}

} // namespace z3lx::runtime
