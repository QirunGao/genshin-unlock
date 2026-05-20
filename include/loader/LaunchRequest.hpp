#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace z3lx::loader {
struct LaunchRequest {
    std::filesystem::path gamePath {};
    std::filesystem::path workingDirectory {};
    std::string args {};
    std::vector<std::filesystem::path> dllPaths {};
    bool suspendLoad = false;
};
} // namespace z3lx::loader
