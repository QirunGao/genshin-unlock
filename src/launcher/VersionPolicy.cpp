#include "launcher/VersionPolicy.hpp"
#include "shared/StatusCode.hpp"

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

#include "util/win/File.hpp"

namespace z3lx::launcher {

util::Version GetGameVersion(const std::filesystem::path& gamePath) {
    const auto trimView = [](std::string_view view) -> std::string_view {
        const auto isSpace = [](const unsigned char c) { return std::isspace(c); };
        const auto first = std::ranges::find_if_not(view, isSpace);
        const auto rev   = view | std::views::reverse;
        const auto last  = std::ranges::find_if_not(rev, isSpace).base();
        return (first < last)
            ? view.substr(first - view.begin(), last - first)
            : std::string_view {};
    };

    const std::filesystem::path configPath = gamePath.parent_path() / "config.ini";
    const wil::unique_hfile configFile = wil::open_or_create_file(configPath.c_str());
    const std::string content = util::ReadFile<std::string>(configFile.get());

    for (auto lineRange : std::views::split(content, '\n')) {
        std::string_view line { lineRange };
        auto sep = std::ranges::find(line, '=');
        if (sep == line.end()) {
            continue;
        }
        const std::string_view key   = trimView({ line.begin(), sep });
        const std::string_view value = trimView({ sep + 1, line.end() });
        if (key == "game_version") {
            return util::Version { value };
        }
    }

    THROW_WIN32(ERROR_FILE_NOT_FOUND);
}

shared::VersionEntry CheckVersionCompatibility(
    const util::Version& modVersion,
    const util::Version& gameVersion)
{
    const auto entry = shared::FindVersionEntry(
        static_cast<uint16_t>(gameVersion.GetMajor()),
        static_cast<uint16_t>(gameVersion.GetMinor())
    );

    if (!entry) {
        throw std::runtime_error { std::format(
            "Game version {}.{} is not in the supported version whitelist. "
            "Mod version: {}. "
            "Please check for an updated release.",
            gameVersion.GetMajor(),
            gameVersion.GetMinor(),
            modVersion.ToString()
        )};
    }

    return *entry;
}

} // namespace z3lx::launcher
