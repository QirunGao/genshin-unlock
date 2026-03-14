#include "launcher/GameLocator.hpp"
#include "common/Constants.hpp"

#include <Windows.h>

#include <wil/registry.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <filesystem>

namespace z3lx::launcher {

std::filesystem::path LocateGamePath() {
    wil::unique_hkey key {};
    const wchar_t* executableName = nullptr;

    if (wil::unique_hkey glKey {};
        SUCCEEDED(wil::reg::open_unique_key_nothrow(
            HKEY_CURRENT_USER,
            LR"(SOFTWARE\Cognosphere\HYP\1_0\hk4e_global)",
            glKey
        ))) {
        key = std::move(glKey);
        executableName = common::osGameFileName;
    } else if (wil::unique_hkey cnKey {};
        SUCCEEDED(wil::reg::open_unique_key_nothrow(
            HKEY_CURRENT_USER,
            LR"(SOFTWARE\miHoYo\HYP\1_1\hk4e_cn)",
            cnKey
        ))) {
        key = std::move(cnKey);
        executableName = common::cnGameFileName;
    } else {
        THROW_WIN32(ERROR_FILE_NOT_FOUND);
    }

    return std::filesystem::path {
        wil::reg::get_value_string(key.get(), L"GameInstallPath")
    } / executableName;
}

} // namespace z3lx::launcher
