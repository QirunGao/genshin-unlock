#include "launcher/ModuleValidator.hpp"

#include <glaze/glaze.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <stdexcept>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

template <>
struct glz::meta<z3lx::launcher::ModuleHashManifest> {
    using T = z3lx::launcher::ModuleHashManifest;
    static constexpr auto value = object(
        "launcherSha256", &T::launcherSha256,
        "bootstrapSha256", &T::bootstrapSha256,
        "runtimeSha256", &T::runtimeSha256
    );
};

namespace z3lx::launcher {

std::string ComputeFileSha256(const std::filesystem::path& filePath) {
    // Open the file
    const HANDLE fileHandle = CreateFileW(
        filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return {};
    }

    // Open an algorithm handle
    BCRYPT_ALG_HANDLE algHandle = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algHandle, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        CloseHandle(fileHandle);
        return {};
    }

    // Create the hash object
    DWORD hashObjSize = 0;
    DWORD cbData = 0;
    status = BCryptGetProperty(algHandle, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PBYTE>(&hashObjSize), sizeof(hashObjSize),
        &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(algHandle, 0);
        CloseHandle(fileHandle);
        return {};
    }

    std::vector<uint8_t> hashObj(hashObjSize);
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    status = BCryptCreateHash(algHandle, &hashHandle,
        hashObj.data(), hashObjSize, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(algHandle, 0);
        CloseHandle(fileHandle);
        return {};
    }

    // Read and hash in chunks
    constexpr DWORD kChunkSize = 65536;
    std::vector<uint8_t> buffer(kChunkSize);
    DWORD bytesRead = 0;
    while (ReadFile(fileHandle, buffer.data(), kChunkSize,
               &bytesRead, nullptr) && bytesRead > 0) {
        status = BCryptHashData(hashHandle, buffer.data(), bytesRead, 0);
        if (!BCRYPT_SUCCESS(status)) {
            CloseHandle(fileHandle);
            BCryptDestroyHash(hashHandle);
            BCryptCloseAlgorithmProvider(algHandle, 0);
            return {};
        }
    }
    CloseHandle(fileHandle);

    // Finalize
    DWORD hashSize = 0;
    status = BCryptGetProperty(algHandle, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PBYTE>(&hashSize), sizeof(hashSize),
        &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hashHandle);
        BCryptCloseAlgorithmProvider(algHandle, 0);
        return {};
    }

    std::vector<uint8_t> hashValue(hashSize);
    status = BCryptFinishHash(hashHandle, hashValue.data(), hashSize, 0);
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);

    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    // Convert to lowercase hex string
    std::string hex;
    hex.reserve(hashSize * 2);
    for (const auto byte : hashValue) {
        hex += std::format("{:02x}", byte);
    }
    return hex;
}

StatusCode ValidateModuleHash(
    const std::filesystem::path& modulePath,
    const std::string& expectedSha256Hex) {
    if (!std::filesystem::exists(modulePath)) {
        return StatusCode::RuntimeLoadFailed;
    }

    const std::string actual = ComputeFileSha256(modulePath);
    if (actual.empty()) {
        return StatusCode::ModuleSignatureInvalid;
    }

    if (expectedSha256Hex.empty()) {
        return StatusCode::ModuleSignatureInvalid;
    }

    if (actual != expectedSha256Hex) {
        return StatusCode::ModuleSignatureInvalid;
    }

    return StatusCode::Ok;
}

ModuleHashManifest ReadModuleHashManifest(
    const std::filesystem::path& manifestPath) {
    if (!std::filesystem::exists(manifestPath)) {
        throw std::runtime_error(std::format(
            "Hash manifest not found: {}", manifestPath.string()));
    }

    std::ifstream input { manifestPath, std::ios::binary };
    if (!input) {
        throw std::runtime_error(std::format(
            "Failed to open hash manifest: {}", manifestPath.string()));
    }

    const std::string content {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> {}
    };

    ModuleHashManifest manifest {};
    const auto error = glz::read_json(manifest, content);
    if (error) {
        throw std::runtime_error(std::format(
            "Failed to parse hash manifest: {}",
            glz::format_error(error, content)));
    }

    return manifest;
}

} // namespace z3lx::launcher
