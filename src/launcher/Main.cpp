#include "launcher/AppConfig.hpp"
#include "launcher/GameLocator.hpp"
#include "launcher/HandshakeClient.hpp"
#include "launcher/Injector.hpp"
#include "launcher/ProcessStarter.hpp"
#include "launcher/VersionPolicy.hpp"
#include "util/win/Dialogue.hpp"
#include "util/win/File.hpp"
#include "util/win/Loader.hpp"
#include "util/win/Shell.hpp"
#include "util/win/Version.hpp"

#include <wil/filesystem.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <thread>

#include <Windows.h>

namespace fs = std::filesystem;
namespace z {
using namespace z3lx::launcher;
using namespace z3lx::util;
} // namespace z

namespace {

// ── Config I/O helpers ────────────────────────────────────────────────────────

template <typename T>
T ReadOrCreate(const fs::path& filePath) {
    T obj {};
    std::vector<uint8_t> buf {};
    const wil::unique_hfile file = wil::open_or_create_file(filePath.c_str());
    try {
        z::ReadFile(file.get(), buf);
        obj.Deserialize(buf);
    } catch (...) {
        obj = {};
        obj.Serialize(buf);
        z::WriteFile(file.get(), buf);
    }
    return obj;
}

template <typename T>
void WriteConfig(const fs::path& filePath, T& obj) {
    std::vector<uint8_t> buf {};
    obj.Serialize(buf);
    const wil::unique_hfile file = wil::open_or_create_file(filePath.c_str());
    z::WriteFile(file.get(), buf);
}

// ── Locate / validate launcher config's gamePath ─────────────────────────────

z::LauncherConfig ResolveConfig(const fs::path& cfgPath) {
    auto cfg = ReadOrCreate<z::LauncherConfig>(cfgPath);
    bool dirty = false;

    const auto isValidGame = [](const fs::path& p) {
        if (!fs::exists(p) || !fs::is_regular_file(p)) {
            return false;
        }
        const auto name = p.filename();
        return name == L"GenshinImpact.exe" || name == L"YuanShen.exe";
    };

    if (!isValidGame(cfg.gamePath)) {
        dirty = true;
        // Try registry first
        bool located = false;
        try {
            cfg.gamePath = z::LocateGamePath();
            located = true;
        } catch (...) {}

        if (!located || !isValidGame(cfg.gamePath)) {
            const auto result = z3lx::util::ShowMessageBox(
                "Launcher",
                std::format(
                    "Could not locate the game executable automatically.\n"
                    "Locate GenshinImpact.exe / YuanShen.exe manually?"),
                z3lx::util::MessageBoxIcon::Warning,
                z3lx::util::MessageBoxButton::YesNo
            );
            if (result == z3lx::util::MessageBoxResult::No) {
                std::exit(0);
            }
            constexpr z3lx::util::Filter filters[] {
                { L"GenshinImpact.exe", L"GenshinImpact.exe" },
                { L"YuanShen.exe",      L"YuanShen.exe" },
            };
            cfg.gamePath = z3lx::util::OpenFileDialogue(filters);
        }
    }

    if (dirty) {
        WriteConfig(cfgPath, cfg);
    }

    return cfg;
}

} // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main() try {
    // Set up WIL failure logging to stderr
    wil::SetResultLoggingCallback([](const wil::FailureInfo& info) noexcept {
        std::array<wchar_t, 2048> buf {};
        if (SUCCEEDED(wil::GetFailureLogString(buf.data(), buf.size(), info))) {
            std::wcerr << buf.data();
        }
    });

    const fs::path selfDir =
        z3lx::util::GetCurrentModuleFilePath().parent_path();

    // ── Step 1: Read configuration ────────────────────────────────────────────
    std::println(std::cout, "Reading launcher configuration...");
    const z::LauncherConfig launConfig =
        ResolveConfig(selfDir / "launcher_config.json");

    std::println(std::cout, "Reading runtime configuration...");
    const auto runtimeCfgFile =
        ReadOrCreate<z::RuntimeConfigFile>(selfDir / "runtime_config.json");
    const z3lx::shared::RuntimeConfig& runtimeConfig = runtimeCfgFile.config;

    // ── Step 2: Version compatibility check ───────────────────────────────────
    std::println(std::cout, "Checking version compatibility...");
    const z3lx::util::Version modVersion =
        z3lx::util::GetFileVersion(z3lx::util::GetCurrentModuleFilePath());

    uint16_t gameVerMajor = 0;
    uint16_t gameVerMinor = 0;

    try {
        const z3lx::util::Version gameVersion =
            z::GetGameVersion(launConfig.gamePath);
        const auto entry =
            z::CheckVersionCompatibility(modVersion, gameVersion);
        gameVerMajor = entry.major;
        gameVerMinor = entry.minor;
        std::println(std::cout, "  Game version {}.{} supported (profile {}).",
            entry.major, entry.minor,
            static_cast<int>(entry.profile));
    } catch (const std::exception& e) {
        const auto result = z3lx::util::ShowMessageBox(
            "Launcher",
            std::format(
                "Version compatibility check failed:\n{}\n\n"
                "Open the download page?", e.what()),
            z3lx::util::MessageBoxIcon::Warning,
            z3lx::util::MessageBoxButton::YesNo
        );
        if (result == z3lx::util::MessageBoxResult::Yes) {
            z3lx::util::OpenUrl(
                "https://github.com/z3lx/genshin-unlock/releases/latest"
            );
        }
        std::exit(0);
    }

    // ── Step 3: Start game process (suspended) ────────────────────────────────
    std::println(std::cout, "Starting game process (suspended)...");
    auto gameProc = z::StartGameProcess(launConfig);

    // ── Step 4: Create handshake pipe server ──────────────────────────────────
    std::println(std::cout, "Creating IPC pipe server (pid={})...",
                 gameProc.processId);
    z::HandshakeClient ipc { gameProc.processId };

    // ── Step 5: Inject bootstrap.dll ─────────────────────────────────────────
    const fs::path bootstrapPath = selfDir / "bootstrap.dll";
    std::println(std::cout, "Injecting bootstrap.dll...");
    z::InjectLibrary(gameProc.process.get(), bootstrapPath);

    // ── Step 6: Bootstrap handshake ───────────────────────────────────────────
    std::println(std::cout, "Waiting for bootstrap handshake...");
    ipc.WaitForBootstrap();

    const fs::path runtimePath = selfDir / "runtime.dll";
    ipc.SendRuntimePath(runtimePath);
    ipc.SendConfigSnapshot(runtimeConfig, gameVerMajor, gameVerMinor);

    std::println(std::cout, "Waiting for runtime initialization result...");
    const auto initResult = ipc.ReceiveInitResult();

    if (initResult.status != z3lx::shared::StatusCode::Ok) {
        throw std::runtime_error { std::format(
            "Runtime initialization failed: {}",
            z3lx::shared::StatusCodeToString(initResult.status)
        )};
    }

    std::println(std::cout, "Runtime initialized (fps={} fov={}).",
        initResult.fpsAvailable, initResult.fovAvailable);

    // ── Step 7: Resume game main thread ───────────────────────────────────────
    std::println(std::cout, "Resuming game process...");
    THROW_LAST_ERROR_IF(
        ResumeThread(gameProc.mainThread.get()) == static_cast<DWORD>(-1)
    );

    if (launConfig.closeLauncherOnSuccess) {
        std::println(std::cout, "closeLauncherOnSuccess=true – exiting.");
        return 0;
    }

    // ── Step 8: Monitor runtime until process exits ───────────────────────────
    ipc.MonitorRuntime();

    std::println(std::cout, "Launcher exiting.");
    return 0;

} catch (const std::exception& e) {
    LOG_CAUGHT_EXCEPTION();
    try {
        z3lx::util::ShowMessageBox(
            "Launcher",
            std::format("An error occurred:\n{}", e.what()),
            z3lx::util::MessageBoxIcon::Error,
            z3lx::util::MessageBoxButton::Ok
        );
    } catch (...) {}
    return 1;
}
