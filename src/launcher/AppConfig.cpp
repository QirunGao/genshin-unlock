#include "launcher/AppConfig.hpp"
#include "shared/ConfigModel.hpp"
#include "util/win/File.hpp"

#include <glaze/glaze.hpp>
#include <glaze/glaze_exceptions.hpp>
#include <wil/filesystem.h>
#include <wil/resource.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace {
constexpr glz::opts opts {
    .null_terminated = false,
    .comments = false,
    .error_on_unknown_keys = true,
    .skip_null_members = false,
    .prettify = true,
    .minified = false,
    .indentation_char = ' ',
    .indentation_width = 4,
    .new_lines_in_arrays = true,
    .append_arrays = false,
    .error_on_missing_keys = true,
    .error_on_const_read = true,
    .bools_as_numbers = false,
    .quoted_num = false,
    .number = false,
    .raw = false,
    .raw_string = false,
    .structs_as_arrays = false,
    .partial_read = false
};
} // namespace

template <>
struct glz::meta<z3lx::shared::DisplayMode> {
    using enum z3lx::shared::DisplayMode;
    static constexpr auto value = enumerate(
        Windowed,
        Fullscreen,
        Borderless
    );
};

namespace z3lx::shared {
void LauncherConfig::Serialize(std::vector<uint8_t>& buffer) {
    glz::ex::write<opts>(*this, buffer);
}

void LauncherConfig::Deserialize(const std::vector<uint8_t>& buffer) {
    glz::ex::read<opts>(*this, buffer);
}
} // namespace z3lx::shared

template <>
struct glz::meta<z3lx::util::VirtualKey> {
    using enum z3lx::util::VirtualKey;
    static constexpr auto value = enumerate(
        LeftMouse,
        RightMouse,
        MiddleMouse,
        X1Mouse,
        X2Mouse,
        Backspace,
        Tab,
        Clear,
        Enter,
        Shift,
        Ctrl,
        Alt,
        Pause,
        CapsLock,
        Esc,
        Space,
        PageUp,
        PageDown,
        End,
        Home,
        LeftArrow,
        UpArrow,
        RightArrow,
        DownArrow,
        PrintScreen,
        Insert,
        Delete,
        "0", D0,
        "1", D1,
        "2", D2,
        "3", D3,
        "4", D4,
        "5", D5,
        "6", D6,
        "7", D7,
        "8", D8,
        "9", D9,
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftWindows,
        RightWindows,
        Apps,
        Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
        Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
        NumpadMultiply,
        NumpadAdd,
        NumpadSeparator,
        NumpadSubtract,
        NumpadDecimal,
        NumpadDivide,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        NumLock,
        ScrollLock,
        LeftShift,
        RightShift,
        LeftCtrl,
        RightCtrl,
        LeftAlt,
        RightAlt,
        Oem1,
        "Plus", OemPlus,
        "Comma", OemComma,
        "Minus", OemMinus,
        "Period", OemPeriod,
        Oem2, Oem3, Oem4, Oem5, Oem6, Oem7, Oem8, Oem102,
        OemClear
    );
};

template <>
struct glz::meta<z3lx::shared::RuntimeConfig> {
    using T = z3lx::shared::RuntimeConfig;

    static constexpr auto fpsConstraintCond = [](
        const T&, const int fps) -> bool {
        return fps >= -1;
    };
    static constexpr auto fpsConstraint = read_constraint<
        &T::targetFps, fpsConstraintCond,
        "Target FPS must be -1 or greater"
    >;

    static constexpr auto isValidFov = [](const int fov) -> bool {
        return fov > 0 && fov < 180;
    };
    static constexpr auto fovConstraintCond = [](
        const T&, const int fov) -> bool {
        return isValidFov(fov);
    };
    static constexpr auto fovConstraint = read_constraint<
        &T::targetFov, fovConstraintCond,
        "Target FOV must be between 1 and 179 degrees"
    >;

    static constexpr auto fovPresetsConstraintCond = [](
        const T&, std::vector<int>& fovPresets) -> bool {
        if (!fovPresets.empty() &&
            std::ranges::all_of(fovPresets, isValidFov)) {
            std::ranges::sort(fovPresets);
            const auto last = std::ranges::unique(fovPresets).begin();
            fovPresets.erase(last, fovPresets.end());
            return true;
        }
        return false;
    };
    static constexpr auto fovPresetsConstraint = read_constraint<
        &T::fovPresets, fovPresetsConstraintCond,
        "FOV presets must be between 1 and 179 degrees"
    >;

    static constexpr auto fovSmoothingConstraintCond = [](
        const T&, const float smoothing) -> bool {
        return smoothing >= 0.0f && smoothing <= 1.0f;
    };
    static constexpr auto fovSmoothingConstraint = read_constraint<
        &T::fovSmoothing, fovSmoothingConstraintCond,
        "FOV smoothing must be between 0.0 and 1.0"
    >;

    static constexpr auto value = object(
        &T::unlockFps,
        "targetFps", fpsConstraint,
        &T::autoThrottle,
        &T::unlockFov,
        "targetFov", fovConstraint,
        "fovPresets", fovPresetsConstraint,
        "fovSmoothing", fovSmoothingConstraint,
        &T::unlockFovKey,
        &T::nextFovPresetKey,
        &T::prevFovPresetKey
    );
};

namespace z3lx::shared {
void RuntimeConfig::Serialize(std::vector<uint8_t>& buffer) {
    glz::ex::write<opts>(*this, buffer);
}

void RuntimeConfig::Deserialize(const std::vector<uint8_t>& buffer) {
    glz::ex::read<opts>(*this, buffer);
}
} // namespace z3lx::shared

namespace z3lx::launcher {

LauncherConfig ReadLauncherConfig(const std::filesystem::path& configFilePath) {
    LauncherConfig config {};
    std::vector<uint8_t> buffer {};
    const wil::unique_hfile configFile = wil::open_or_create_file(
        configFilePath.c_str());
    util::ReadFile(configFile.get(), buffer);
    config.Deserialize(buffer);
    return config;
}

RuntimeConfig ReadRuntimeConfig(const std::filesystem::path& configFilePath) {
    RuntimeConfig config {};
    std::vector<uint8_t> buffer {};
    const wil::unique_hfile configFile = wil::open_or_create_file(
        configFilePath.c_str());
    util::ReadFile(configFile.get(), buffer);
    config.Deserialize(buffer);
    return config;
}

void WriteLauncherConfig(
    const std::filesystem::path& configFilePath,
    const LauncherConfig& config) {
    std::vector<uint8_t> buffer {};
    LauncherConfig mutable_config = config;
    mutable_config.Serialize(buffer);
    const wil::unique_hfile configFile = wil::open_or_create_file(
        configFilePath.c_str());
    util::WriteFile(configFile.get(), buffer);
}

void WriteRuntimeConfig(
    const std::filesystem::path& configFilePath,
    const RuntimeConfig& config) {
    std::vector<uint8_t> buffer {};
    RuntimeConfig mutable_config = config;
    mutable_config.Serialize(buffer);
    const wil::unique_hfile configFile = wil::open_or_create_file(
        configFilePath.c_str());
    util::WriteFile(configFile.get(), buffer);
}

} // namespace z3lx::launcher
