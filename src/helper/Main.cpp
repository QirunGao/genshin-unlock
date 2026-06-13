#include "common/PluginHash.hpp"
#include "loader/LaunchPlan.hpp"
#include "util/win/File.hpp"
#include "util/win/Loader.hpp"
#include "util/win/String.hpp"

#include <wil/filesystem.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>
#include <softpub.h>
#include <wintrust.h>

namespace z {
using namespace z3lx::common;
using namespace z3lx::loader;
using namespace z3lx::util;
} // namespace z

namespace {
std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    const wil::unique_hfile file = wil::open_or_create_file(path.c_str());
    return z::ReadFile<std::vector<uint8_t>>(file.get());
}

std::array<uint8_t, 32> Sha256(const std::span<const uint8_t> buffer) {
    BCRYPT_ALG_HANDLE algorithm {};
    THROW_IF_NTSTATUS_FAILED(BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0
    ));
    const auto algorithmCleanup = wil::scope_exit([&] {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    });

    BCRYPT_HASH_HANDLE hash {};
    THROW_IF_NTSTATUS_FAILED(BCryptCreateHash(
        algorithm,
        &hash,
        nullptr,
        0,
        nullptr,
        0,
        0
    ));
    const auto hashCleanup = wil::scope_exit([&] {
        BCryptDestroyHash(hash);
    });

    THROW_IF_NTSTATUS_FAILED(BCryptHashData(
        hash,
        const_cast<PUCHAR>(buffer.data()),
        static_cast<ULONG>(buffer.size()),
        0
    ));

    std::array<uint8_t, 32> digest {};
    THROW_IF_NTSTATUS_FAILED(BCryptFinishHash(
        hash,
        digest.data(),
        static_cast<ULONG>(digest.size()),
        0
    ));
    return digest;
}

std::string Hex(const std::span<const uint8_t> buffer) {
    constexpr char alphabet[] = "0123456789ABCDEF";
    std::string result {};
    result.reserve(buffer.size() * 2);
    for (const uint8_t byte : buffer) {
        result.push_back(alphabet[byte >> 4]);
        result.push_back(alphabet[byte & 0x0F]);
    }
    return result;
}

z::LaunchPlan ReadLaunchPlan(const wchar_t* pipeName) {
    const wil::unique_hfile pipe {
        CreateFileW(
            pipeName,
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        )
    };
    THROW_LAST_ERROR_IF(!pipe);

    uint32_t planBufferSize = 0;
    DWORD bytesRead = 0;
    THROW_IF_WIN32_BOOL_FALSE(::ReadFile(
        pipe.get(),
        &planBufferSize,
        sizeof(planBufferSize),
        &bytesRead,
        nullptr
    ));

    std::vector<uint8_t> planBuffer(planBufferSize);
    THROW_IF_WIN32_BOOL_FALSE(::ReadFile(
        pipe.get(),
        planBuffer.data(),
        planBufferSize,
        &bytesRead,
        nullptr
    ));

    z::LaunchPlan plan {};
    plan.Deserialize(planBuffer);
    return plan;
}

void VerifyFileSignature(const std::filesystem::path& path) {
    WINTRUST_FILE_INFO fileInfo {
        .cbStruct = sizeof(fileInfo),
        .pcwszFilePath = path.c_str()
    };

    WINTRUST_DATA trustData {
        .cbStruct = sizeof(trustData),
        .dwUIChoice = WTD_UI_NONE,
        .fdwRevocationChecks = WTD_REVOKE_NONE,
        .dwUnionChoice = WTD_CHOICE_FILE,
        .pFile = &fileInfo,
        .dwStateAction = WTD_STATEACTION_VERIFY
    };

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &action, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trustData);
    THROW_HR_IF(static_cast<HRESULT>(result), result != ERROR_SUCCESS);
}

void StartGame(
    const z::LaunchPlan& plan,
    const std::filesystem::path& pluginPath) {
    if (Hex(Sha256(ReadFile(pluginPath))) != z::pluginHash) {
        throw std::invalid_argument { "Invalid plugin hash" };
    }
    VerifyFileSignature(plan.gamePath);

    std::wstring args {};
    z::U8ToU16(plan.args, args);

    STARTUPINFOW si { .cb = sizeof(si) };
    PROCESS_INFORMATION pi {};
    THROW_IF_WIN32_BOOL_FALSE(CreateProcessW(
        plan.gamePath.c_str(),
        args.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        plan.workingDirectory.c_str(),
        &si,
        &pi
    ));
    const wil::unique_handle process { pi.hProcess };
    const wil::unique_handle thread { pi.hThread };

    z::LoadRemoteLibrary(process.get(), pluginPath);
}
} // namespace

int wmain(const int argc, wchar_t* argv[]) try {
    if (argc != 2) {
        throw std::invalid_argument { "Missing launch pipe name" };
    }

    const std::filesystem::path currentPath =
        z::GetCurrentModuleFilePath().parent_path();
    const z::LaunchPlan plan = ReadLaunchPlan(argv[1]);
    StartGame(plan, currentPath / "GenshinUnlockPlugin.dll");
    return 0;
} catch (...) {
    return wil::ResultFromCaughtException();
}
