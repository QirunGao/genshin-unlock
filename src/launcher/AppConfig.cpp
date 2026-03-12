#include "launcher/AppConfig.hpp"
#include "util/win/VirtualKey.hpp"

#include <glaze/glaze.hpp>
#include <glaze/glaze_exceptions.hpp>

#include <algorithm>
#include <cstdint>
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
struct glz::meta<z3lx::launcher::DisplayMode> {
    using enum z3lx::launcher::DisplayMode;
    static constexpr auto value = enumerate(Windowed, Fullscreen, Borderless);
};

template <>
struct glz::meta<z3lx::launcher::LogLevel> {
    using enum z3lx::launcher::LogLevel;
    static constexpr auto value = enumerate(Debug, Info, Warning, Error);
};

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
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
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

    static constexpr auto fpsCond = [](
        const T&, const int fps) -> bool {
        return fps >= -1;
    };
    static constexpr auto fpsConstraint = read_constraint<
        &T::targetFps, fpsCond,
        "Target FPS must be -1 or greater">;

    static constexpr auto isValidFov = [](const uint8_t fov) -> bool {
        return fov > 0 && fov < 180;
    };
    static constexpr auto fovCond = [](
        const T&, const int fov) -> bool {
        return isValidFov(fov);
    };
    static constexpr auto fovConstraint = read_constraint<
        &T::targetFov, fovCond,
        "Target FOV must be between 1 and 179 degrees">;

    static constexpr auto fovPresetsCond = [](
        const T&, std::vector<int>& presets) -> bool {
        if (!presets.empty() &&
            std::ranges::all_of(presets, isValidFov)) {
            std::ranges::sort(presets);
            const auto last = std::ranges::unique(presets).begin();
            presets.erase(last, presets.end());
            return true;
        }
        return false;
    };
    static constexpr auto fovPresetsConstraint = read_constraint<
        &T::fovPresets, fovPresetsCond,
        "FOV presets must be between 1 and 179 degrees">;

    static constexpr auto smoothCond = [](
        const T&, const float s) -> bool {
        return s >= 0.0f && s <= 1.0f;
    };
    static constexpr auto smoothConstraint = read_constraint<
        &T::fovSmoothing, smoothCond,
        "FOV smoothing must be between 0.0 and 1.0">;

    static constexpr auto value = object(
        &T::unlockFps,
        "targetFps",    fpsConstraint,
        &T::autoThrottle,
        &T::unlockFov,
        "targetFov",    fovConstraint,
        "fovPresets",   fovPresetsConstraint,
        "fovSmoothing", smoothConstraint,
        &T::unlockFovKey,
        &T::nextFovPresetKey,
        &T::prevFovPresetKey
    );
};

namespace z3lx::launcher {

void LauncherConfig::Serialize(std::vector<uint8_t>& buffer) {
    glz::ex::write<opts>(*this, buffer);
}

void LauncherConfig::Deserialize(const std::vector<uint8_t>& buffer) {
    glz::ex::read<opts>(*this, buffer);
}

void RuntimeConfigFile::Serialize(std::vector<uint8_t>& buffer) {
    glz::ex::write<opts>(config, buffer);
}

void RuntimeConfigFile::Deserialize(const std::vector<uint8_t>& buffer) {
    glz::ex::read<opts>(config, buffer);
}

} // namespace z3lx::launcher
