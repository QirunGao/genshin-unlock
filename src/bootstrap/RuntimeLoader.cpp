#include "bootstrap/RuntimeLoader.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace z3lx::bootstrap {

namespace {

std::string ComputeFileSha256(const std::filesystem::path& filePath) {
    const HANDLE fileHandle = CreateFileW(
        filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return {};
    }

    BCRYPT_ALG_HANDLE algHandle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algHandle, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        CloseHandle(fileHandle);
        return {};
    }

    DWORD hashObjectSize = 0;
    DWORD bytesWritten = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algHandle, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hashObjectSize),
            sizeof(hashObjectSize), &bytesWritten, 0))) {
        BCryptCloseAlgorithmProvider(algHandle, 0);
        CloseHandle(fileHandle);
        return {};
    }

    std::vector<std::uint8_t> hashObject(hashObjectSize);
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algHandle, &hashHandle, hashObject.data(),
            hashObjectSize, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(algHandle, 0);
        CloseHandle(fileHandle);
        return {};
    }

    std::array<std::uint8_t, 64 * 1024> buffer {};
    DWORD bytesRead = 0;
    while (ReadFile(fileHandle, buffer.data(),
               static_cast<DWORD>(buffer.size()),
               &bytesRead, nullptr) && bytesRead > 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(
                hashHandle, buffer.data(), bytesRead, 0))) {
            BCryptDestroyHash(hashHandle);
            BCryptCloseAlgorithmProvider(algHandle, 0);
            CloseHandle(fileHandle);
            return {};
        }
    }
    CloseHandle(fileHandle);

    DWORD hashSize = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algHandle, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize),
            sizeof(hashSize), &bytesWritten, 0))) {
        BCryptDestroyHash(hashHandle);
        BCryptCloseAlgorithmProvider(algHandle, 0);
        return {};
    }

    std::vector<std::uint8_t> hashValue(hashSize);
    const NTSTATUS finishStatus =
        BCryptFinishHash(hashHandle, hashValue.data(), hashSize, 0);
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    if (!BCRYPT_SUCCESS(finishStatus)) {
        return {};
    }

    std::string hex;
    hex.reserve(hashSize * 2);
    for (const auto byte : hashValue) {
        hex += std::format("{:02x}", byte);
    }
    return hex;
}

} // namespace

RuntimeLoadResult LoadRuntime(
    const std::filesystem::path& runtimePath,
    const std::string& expectedSha256Hex) {
    RuntimeLoadResult result {};

    // Reject relative paths
    if (runtimePath.is_relative()) {
        result.status = StatusCode::ModuleSignatureInvalid;
        result.message = "Runtime path must be absolute.";
        return result;
    }

    // Reject network paths
    const std::wstring pathStr = runtimePath.native();
    if (pathStr.starts_with(L"\\\\")) {
        result.status = StatusCode::ModuleSignatureInvalid;
        result.message = "Runtime path must not be a network path.";
        return result;
    }

    // Verify file exists
    if (!std::filesystem::exists(runtimePath)) {
        result.status = StatusCode::RuntimeLoadFailed;
        result.message = "runtime.dll was not found on disk.";
        return result;
    }

    if (expectedSha256Hex.empty()) {
        result.status = StatusCode::ModuleSignatureInvalid;
        result.message = "Runtime SHA-256 manifest entry is missing.";
        return result;
    }

    const std::string actualSha256 = ComputeFileSha256(runtimePath);
    if (actualSha256.empty() || actualSha256 != expectedSha256Hex) {
        result.status = StatusCode::ModuleSignatureInvalid;
        result.message =
            "runtime.dll failed SHA-256 validation against the manifest.";
        return result;
    }

    // Load the module
    const HMODULE module = LoadLibraryW(runtimePath.c_str());
    if (!module) {
        result.status = StatusCode::RuntimeLoadFailed;
        result.systemError = GetLastError();
        result.message = "LoadLibraryW failed while loading runtime.dll.";
        return result;
    }

    result.runtimeModule = module;
    result.message = "runtime.dll loaded successfully.";
    result.status = StatusCode::Ok;
    return result;
}

void UnloadRuntime(const HMODULE runtimeModule) {
    if (runtimeModule) {
        FreeLibrary(runtimeModule);
    }
}

} // namespace z3lx::bootstrap
