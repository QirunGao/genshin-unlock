#include "launcher/VersionPolicy.hpp"
#include "util/win/File.hpp"
#include "util/win/Version.hpp"

#include <wil/filesystem.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace z3lx::launcher {

util::Version ReadGameVersion(const std::filesystem::path& gamePath) {
    const auto trimView = [](std::string_view view) -> std::string_view {
        const auto isSpace = [](const unsigned char c) -> bool {
            return std::isspace(c);
        };
        const auto first = std::ranges::find_if_not(view, isSpace);
        const auto reverseView = view | std::views::reverse;
        const auto last = std::ranges::find_if_not(reverseView, isSpace).base();
        return (first < last)
            ? view.substr(first - view.begin(), last - first)
            : std::string_view {};
    };

    const std::filesystem::path configFilePath =
        gamePath.parent_path() / "config.ini";
    const wil::unique_hfile configFile { CreateFileW(
        configFilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ) };
    THROW_LAST_ERROR_IF(!configFile.is_valid());
    const std::string content =
        util::ReadFile<std::string>(configFile.get());

    for (auto lineRange : std::views::split(content, '\n')) {
        std::string_view line { lineRange };
        auto sep = std::ranges::find(line, '=');
        if (sep == line.end()) {
            continue;
        }
        const std::string_view key =
            trimView(std::string_view { line.begin(), sep });
        const std::string_view value =
            trimView(std::string_view { sep + 1, line.end() });
        if (key == "game_version") {
            return util::Version { value };
        }
    }

    THROW_WIN32(ERROR_FILE_NOT_FOUND);
}

bool IsVersionSupported(
    const util::Version& gameVersion,
    const VersionTable& table) {
    return table.IsSupported(gameVersion);
}

void CheckVersionCompatibility(
    const util::Version& toolVersion,
    const util::Version& gameVersion,
    const VersionTable& table) {
    (void)toolVersion;
    if (!table.IsSupported(gameVersion)) {
        throw std::runtime_error(std::format(
            "Game version {} is not in the supported version whitelist",
            gameVersion.ToString()));
    }
}

} // namespace z3lx::launcher
