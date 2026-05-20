#pragma once

#include "loader/LaunchRequest.hpp"

#include <cstdint>
#include <vector>

namespace z3lx::loader {
constexpr uint32_t helperProtocolMagic = 0x5A334C58;
constexpr uint16_t helperProtocolVersion = 1;
constexpr uint16_t helperRequestTypeLaunch = 1;

struct HelperRequest {
    uint32_t magic = helperProtocolMagic;
    uint16_t version = helperProtocolVersion;
    uint16_t type = helperRequestTypeLaunch;
    uint64_t nonce = 0;
    LaunchRequest launchRequest {};

    [[nodiscard]] bool IsValid(uint64_t expectedNonce) const;

    void Serialize(std::vector<uint8_t>& buffer) const;
    void Deserialize(const std::vector<uint8_t>& buffer);
};
} // namespace z3lx::loader
