#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace z3lx::loader {
struct LaunchPlan {
    std::filesystem::path gamePath {};
    std::filesystem::path workingDirectory {};
    std::string args {};

    void Serialize(std::vector<uint8_t>& buffer) const;
    void Deserialize(const std::vector<uint8_t>& buffer);
};

} // namespace z3lx::loader
