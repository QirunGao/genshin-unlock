#include "runtime/FpsService.hpp"
#include "runtime/HookManager.hpp"
#include "runtime/RuntimeState.hpp"
#include "shared/ConfigModel.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace z = z3lx;

namespace {

void Expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

void TestRuntimeStateTransitions() {
    z::runtime::RuntimeState state;

    Expect(state.GetState() == z::runtime::State::Created,
        "Runtime state should start in Created.");
    Expect(state.TransitionTo(z::runtime::State::HostValidated),
        "Created -> HostValidated should succeed.");
    Expect(state.TransitionTo(z::runtime::State::IpcReady),
        "HostValidated -> IpcReady should succeed.");
    Expect(!state.TransitionTo(z::runtime::State::Running),
        "IpcReady -> Running should be rejected.");
    Expect(state.TransitionTo(z::runtime::State::ConfigReady),
        "IpcReady -> ConfigReady should succeed.");
    Expect(state.TransitionTo(z::runtime::State::SymbolsResolved),
        "ConfigReady -> SymbolsResolved should succeed.");
    Expect(state.TransitionTo(z::runtime::State::HooksInstalled),
        "SymbolsResolved -> HooksInstalled should succeed.");
    Expect(state.TransitionTo(z::runtime::State::Running),
        "HooksInstalled -> Running should succeed.");
    Expect(!state.IsTerminal(),
        "Running should not be terminal.");
    Expect(state.TransitionTo(z::runtime::State::Fatal),
        "Running -> Fatal should succeed.");
    Expect(state.IsTerminal(),
        "Fatal should be terminal.");
}

void TestHookManagerLifecycle() {
    z::runtime::HookManager manager;
    bool installCalled = false;
    bool uninstallCalled = false;
    bool enabledValue = false;
    int callbackCount = 0;

    manager.SetStateChangeCallback(
        [&](const std::string&, const bool, const bool) {
            ++callbackCount;
        });

    Expect(manager.Register({
        .name = "TestHook",
        .installFn = [&]() {
            installCalled = true;
            return true;
        },
        .uninstallFn = [&]() {
            uninstallCalled = true;
        },
        .setEnabledFn = [&](const bool enabled) {
            enabledValue = enabled;
        }
    }) == z::shared::StatusCode::Ok,
        "Hook registration should succeed.");

    Expect(manager.InstallAll() == z::shared::StatusCode::Ok,
        "Hook installation should succeed.");
    Expect(installCalled,
        "Hook install callback should be invoked.");
    Expect(manager.IsInstalled("TestHook"),
        "Hook should be marked installed.");

    Expect(manager.Enable("TestHook") == z::shared::StatusCode::Ok,
        "Hook enable should succeed.");
    Expect(enabledValue,
        "Hook enable callback should receive true.");
    Expect(manager.IsEnabled("TestHook"),
        "Hook should be marked enabled.");

    Expect(manager.Disable("TestHook") == z::shared::StatusCode::Ok,
        "Hook disable should succeed.");
    Expect(!enabledValue,
        "Hook enable callback should receive false on disable.");
    Expect(!manager.IsEnabled("TestHook"),
        "Hook should be marked disabled.");

    Expect(manager.UninstallAll() == z::shared::StatusCode::Ok,
        "Hook uninstall should succeed.");
    Expect(uninstallCalled,
        "Hook uninstall callback should be invoked.");
    Expect(callbackCount >= 3,
        "State change callback should observe lifecycle events.");
}

void TestRuntimeConfigValidation() {
    z::shared::RuntimeConfig config {};
    std::vector<std::uint8_t> serialized {};
    config.Serialize(serialized);

    std::string json(serialized.begin(), serialized.end());
    const auto targetMarker = json.find("\"targetFps\": 120");
    Expect(targetMarker != std::string::npos,
        "Serialized config should contain targetFps.");

    std::string validJson = json;
    validJson.replace(targetMarker, std::string_view("\"targetFps\": 120").size(),
        "\"targetFps\": -1");
    z::shared::RuntimeConfig validConfig {};
    validConfig.Deserialize(std::vector<std::uint8_t>(
        validJson.begin(), validJson.end()));
    Expect(validConfig.targetFps == -1,
        "targetFps=-1 should remain valid.");

    std::string invalidJson = json;
    invalidJson.replace(targetMarker, std::string_view("\"targetFps\": 120").size(),
        "\"targetFps\": 2001");

    bool rejected = false;
    try {
        z::shared::RuntimeConfig invalidConfig {};
        invalidConfig.Deserialize(std::vector<std::uint8_t>(
            invalidJson.begin(), invalidJson.end()));
    } catch (...) {
        rejected = true;
    }
    Expect(rejected,
        "Out-of-range targetFps should be rejected.");
}

void TestFpsServiceClampsTargetRange() {
    z::runtime::FpsService fpsService;

    fpsService.SetTargetFps(5000);
    Expect(fpsService.GetTargetFps() == 1000,
        "FPS target should clamp to the upper bound.");

    fpsService.SetTargetFps(0);
    Expect(fpsService.GetTargetFps() == 1,
        "FPS target should clamp to the lower bound.");

    fpsService.SetTargetFps(-1);
    Expect(fpsService.GetTargetFps() == -1,
        "FPS target should preserve the unlimited sentinel.");
}

} // namespace

int main() try {
    TestRuntimeStateTransitions();
    TestHookManagerLifecycle();
    TestRuntimeConfigValidation();
    TestFpsServiceClampsTargetRange();
    std::cout << "core_tests passed\n";
    return EXIT_SUCCESS;
} catch (const std::exception& e) {
    std::cerr << "core_tests failed: " << e.what() << '\n';
    return EXIT_FAILURE;
}
