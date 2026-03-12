#pragma once

#include <filesystem>

namespace z3lx::launcher {

// Locates the Genshin Impact executable from the Windows registry.
// Tries the Global (OS) build first, then the CN build.
// Throws HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) when neither is found.
[[nodiscard]] std::filesystem::path LocateGamePath();

} // namespace z3lx::launcher
