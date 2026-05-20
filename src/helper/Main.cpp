#include "loader/HelperProtocol.hpp"
#include "loader/LaunchRequest.hpp"
#include "util/win/Loader.hpp"
#include "util/win/PipeMessage.hpp"
#include "util/win/String.hpp"

#include <wil/resource.h>
#include <wil/result.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <Windows.h>

namespace z {
using namespace z3lx::loader;
using namespace z3lx::util;
} // namespace z

namespace {
wil::unique_hfile ConnectPipe(const wchar_t* pipeName) {
    HANDLE pipeHandle = CreateFileW(
        pipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );
    THROW_LAST_ERROR_IF(pipeHandle == INVALID_HANDLE_VALUE);
    return wil::unique_hfile { pipeHandle };
}

std::vector<uint8_t> ToBuffer(const char* text) {
    const auto* begin = reinterpret_cast<const uint8_t*>(text);
    return { begin, begin + std::strlen(text) };
}

uint64_t ParseNonce(const wchar_t* text) {
    size_t index = 0;
    const uint64_t nonce = std::stoull(text, &index, 16);
    if (text[index] != L'\0') {
        throw std::invalid_argument { "Invalid helper nonce" };
    }
    return nonce;
}

z::LaunchRequest ReadLaunchRequest(
    const HANDLE pipe,
    const uint64_t expectedNonce) {
    z::HelperRequest request {};
    request.Deserialize(z::ReadPipeMessage(pipe));
    if (!request.IsValid(expectedNonce)) {
        throw std::invalid_argument { "Invalid helper request" };
    }
    return request.launchRequest;
}

void StartGame(const z::LaunchRequest& request) {
    std::wstring args {};
    z::U8ToU16(request.args, args);

    STARTUPINFOW si { .cb = sizeof(si) };
    PROCESS_INFORMATION pi {};
    THROW_IF_WIN32_BOOL_FALSE(CreateProcessW(
        request.gamePath.c_str(),
        args.data(),
        nullptr,
        nullptr,
        FALSE,
        request.suspendLoad ? CREATE_SUSPENDED : 0,
        nullptr,
        request.workingDirectory.c_str(),
        &si,
        &pi
    ));
    const wil::unique_handle process { pi.hProcess };
    const wil::unique_handle thread { pi.hThread };

    z::LoadRemoteLibrary(process.get(), request.dllPaths);
    if (request.suspendLoad) {
        ResumeThread(thread.get());
    }
}

} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    if (argc != 3) {
        return ERROR_INVALID_PARAMETER;
    }

    wil::unique_hfile pipe {};
    try {
        const uint64_t nonce = ParseNonce(argv[2]);
        pipe = ConnectPipe(argv[1]);
        StartGame(ReadLaunchRequest(pipe.get(), nonce));
        z::WritePipeMessage(pipe.get(), {});
        return 0;
    } catch (const std::exception& e) {
        if (pipe.is_valid()) {
            try {
                z::WritePipeMessage(pipe.get(), ToBuffer(e.what()));
            } catch (...) {}
        }
        return 1;
    }
}
