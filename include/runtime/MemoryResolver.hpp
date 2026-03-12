#pragma once

#include "shared/VersionTable.hpp"
#include "shared/StatusCode.hpp"

#include <cstdint>
#include <optional>

#include <Windows.h>

namespace z3lx::runtime {

using namespace z3lx::shared;

enum class GameRegion : uint8_t {
    OS,
    CN
};

struct ResolvedAddresses {
    int* fpsAddress = nullptr;
    void* fovTarget = nullptr;
    GameRegion region = GameRegion::OS;
};

class MemoryResolver {
public:
    MemoryResolver() noexcept;
    ~MemoryResolver() noexcept;

    StatusCode Resolve(const VersionTable& table,
                       const util::Version& gameVersion);

    [[nodiscard]] std::optional<ResolvedAddresses> GetAddresses() const noexcept;
    [[nodiscard]] bool IsFpsResolved() const noexcept;
    [[nodiscard]] bool IsFovResolved() const noexcept;

private:
    StatusCode ResolveByOffsetTable(const ResolverProfile& profile);

    std::optional<ResolvedAddresses> addresses;
    HMODULE gameModule = nullptr;
    GameRegion region = GameRegion::OS;
};

} // namespace z3lx::runtime
