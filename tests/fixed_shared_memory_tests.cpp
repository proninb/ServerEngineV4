#include "fixed_shared_memory.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cw::server {
namespace {

struct test_state final {
    int failures = 0;

    bool expect(
        bool condition,
        std::string_view name) {

        if (condition) {
            return true;
        }

        ++failures;
        std::cerr
            << "FAILED: "
            << name
            << '\n';

        return false;
    }
};

[[nodiscard]] std::uint64_t process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(
        GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(
        getpid());
#endif
}

[[nodiscard]] std::uintptr_t
reserve_candidate(
    std::size_t size) noexcept {
#if defined(_WIN32)
    auto* value =
        VirtualAlloc(
            nullptr,
            size,
            MEM_RESERVE,
            PAGE_NOACCESS);

    if (value == nullptr) {
        return 0;
    }

    const auto address =
        reinterpret_cast<std::uintptr_t>(
            value);

    VirtualFree(
        value,
        0,
        MEM_RELEASE);

    return address;
#else
#ifdef MAP_ANONYMOUS
    constexpr auto anonymous =
        MAP_ANONYMOUS;
#else
    constexpr auto anonymous =
        MAP_ANON;
#endif

    auto* value =
        mmap(
            nullptr,
            size,
            PROT_NONE,
            MAP_PRIVATE |
                anonymous,
            -1,
            0);

    if (value == MAP_FAILED) {
        return 0;
    }

    const auto address =
        reinterpret_cast<std::uintptr_t>(
            value);

    munmap(
        value,
        size);

    return address;
#endif
}

[[nodiscard]] bool native_name_exists(
    std::string_view name) noexcept {
#if defined(_WIN32)
    std::wstring native;

    try {
        native.reserve(
            name.size());

        for (const auto value : name) {
            native.push_back(
                static_cast<wchar_t>(
                    static_cast<unsigned char>(
                        value)));
        }
    }
    catch (...) {
        return false;
    }

    const auto mapping =
        OpenFileMappingW(
            FILE_MAP_READ,
            FALSE,
            native.c_str());

    if (mapping == nullptr) {
        return false;
    }

    CloseHandle(mapping);
    return true;
#else
    std::string native;

    try {
        native.reserve(
            name.size() + 1);

        native.push_back('/');
        native.append(name);
    }
    catch (...) {
        return false;
    }

    const auto file =
        shm_open(
            native.c_str(),
            O_RDWR,
            0600);

    if (file < 0) {
        return false;
    }

    close(file);
    return true;
#endif
}

template <typename T>
[[nodiscard]] bool parse_unsigned(
    std::string_view text,
    T& output) noexcept {

    output = 0;

    const auto result =
        std::from_chars(
            text.data(),
            text.data() +
                text.size(),
            output);

    return result.ec ==
            std::errc{} &&
        result.ptr ==
            text.data() +
                text.size();
}

[[nodiscard]] int child_mode(
    std::string_view name,
    std::string_view size_text,
    std::string_view address_text) noexcept {

    std::size_t size = 0;
    std::uintptr_t address = 0;

    if (!parse_unsigned(
            size_text,
            size) ||
        !parse_unsigned(
            address_text,
            address)) {

        return 10;
    }

    fixed_shared_memory mapping;

    const auto result =
        mapping.open(
            name,
            size,
            address);

    if (result ==
        fixed_shared_memory_result::
            address_unavailable) {

        return 3;
    }

    if (result !=
        fixed_shared_memory_result::
            success ||
        mapping.address() !=
            address ||
        mapping.size() !=
            size) {

        return 11;
    }

    auto bytes =
        mapping.bytes();

    if (bytes.size() < 3 ||
        bytes[0] !=
            std::byte{0x5a} ||
        bytes[bytes.size() - 1] !=
            std::byte{0x7c}) {

        return 12;
    }

    bytes[1] =
        std::byte{0xa5};

    return 0;
}

#if defined(_WIN32)

[[nodiscard]] int run_child(
    const char*,
    std::string_view name,
    std::size_t size,
    std::uintptr_t address) {

    wchar_t executable[32768]{};

    constexpr auto capacity =
        static_cast<DWORD>(
            sizeof(executable) /
            sizeof(executable[0]));

    const auto length =
        GetModuleFileNameW(
            nullptr,
            executable,
            capacity);

    if (length == 0 ||
        length >= capacity) {

        return -1;
    }

    std::wstring command;

    try {
        command =
            L"\"" +
            std::wstring{
                executable,
                length} +
            L"\" --child ";

        for (const auto value : name) {
            command.push_back(
                static_cast<wchar_t>(
                    static_cast<unsigned char>(
                        value)));
        }

        command +=
            L" " +
            std::to_wstring(
                size) +
            L" " +
            std::to_wstring(
                address);
    }
    catch (...) {
        return -1;
    }

    STARTUPINFOW startup{};
    startup.cb =
        sizeof(startup);

    PROCESS_INFORMATION process{};

    if (CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process) == 0) {

        return -1;
    }

    WaitForSingleObject(
        process.hProcess,
        INFINITE);

    DWORD exit_code =
        static_cast<DWORD>(-1);

    const auto queried =
        GetExitCodeProcess(
            process.hProcess,
            &exit_code);

    CloseHandle(
        process.hThread);

    CloseHandle(
        process.hProcess);

    return queried != 0
        ? static_cast<int>(
            exit_code)
        : -1;
}

#else

[[nodiscard]] int run_child(
    const char* executable_argument,
    std::string_view name,
    std::size_t size,
    std::uintptr_t address) {

    std::error_code error;

    auto executable =
        std::filesystem::absolute(
            executable_argument,
            error);

    if (error) {
        return -1;
    }

    const auto size_text =
        std::to_string(size);

    const auto address_text =
        std::to_string(address);

    std::string name_text{name};

    const auto process =
        fork();

    if (process < 0) {
        return -1;
    }

    if (process == 0) {
        execl(
            executable.c_str(),
            executable.c_str(),
            "--child",
            name_text.c_str(),
            size_text.c_str(),
            address_text.c_str(),
            static_cast<char*>(nullptr));

        _exit(127);
    }

    int status = 0;

    if (waitpid(
            process,
            &status,
            0) != process ||
        !WIFEXITED(status)) {

        return -1;
    }

    return WEXITSTATUS(status);
}

#endif

void test_contract(
    test_state& tests,
    const char* executable) {

    const auto address_alignment =
        fixed_shared_memory::
            address_alignment();

    const auto size_alignment =
        fixed_shared_memory::
            size_alignment();

    if (!tests.expect(
            address_alignment != 0 &&
            size_alignment != 0,
            "query shared-memory alignments")) {

        return;
    }

    const auto minimum =
        static_cast<std::size_t>(
            64 * 1024);

    const auto size =
        ((minimum +
          size_alignment -
          1) /
         size_alignment) *
        size_alignment;

    const auto prefix =
        std::string{
            "CW.ServerEngineV4.Project.Test."} +
        std::to_string(
            process_id());

    bool completed = false;

    for (std::uint32_t attempt = 0;
         attempt < 8 &&
         !completed;
         ++attempt) {

        const auto address =
            reserve_candidate(
                size);

        if (address == 0 ||
            address %
                address_alignment != 0) {

            continue;
        }

        const auto name =
            prefix +
            "-" +
            std::to_string(
                attempt);

        fixed_shared_memory owner;

        const auto created =
            owner.create(
                name,
                size,
                address);

        if (created ==
            fixed_shared_memory_result::
                address_unavailable) {

            continue;
        }

        if (!tests.expect(
                created ==
                    fixed_shared_memory_result::
                        success,
                "create exact-address shared memory")) {

            return;
        }

        if (!tests.expect(
                owner.valid() &&
                owner.address() ==
                    address &&
                owner.size() ==
                    size,
                "created mapping uses requested address and size") ||
            !tests.expect(
                native_name_exists(
                    name),
                "OS shared-memory object uses configured name without hidden prefix")) {

            return;
        }

        auto bytes =
            owner.bytes();

        bytes[0] =
            std::byte{0x5a};

        bytes[
            bytes.size() - 1] =
                std::byte{0x7c};

        fixed_shared_memory duplicate;

        tests.expect(
            duplicate.create(
                name,
                size,
                address) ==
                fixed_shared_memory_result::
                    already_exists &&
            !duplicate.valid(),
            "duplicate shared-memory name fails closed");

        const auto conflict_name =
            name +
            "-conflict";

        fixed_shared_memory conflict;

        tests.expect(
            conflict.create(
                conflict_name,
                size,
                address) ==
                fixed_shared_memory_result::
                    address_unavailable &&
            !conflict.valid(),
            "occupied exact address has no fallback mapping");

        fixed_shared_memory moved{
            std::move(owner)};

        if (!tests.expect(
                !owner.valid() &&
                moved.valid() &&
                moved.address() ==
                    address,
                "move preserves shared-memory ownership")) {

            return;
        }

        const auto child =
            run_child(
                executable,
                name,
                size,
                address);

        if (child == 3) {
            moved.reset();
            continue;
        }

        if (!tests.expect(
                child == 0,
                "child opens same named memory at same virtual address")) {

            return;
        }

        if (!tests.expect(
                moved.bytes()[1] ==
                    std::byte{0xa5},
                "parent observes child shared-memory write")) {

            return;
        }

        moved.reset();

        fixed_shared_memory missing;

        tests.expect(
            missing.open(
                name,
                size,
                address) ==
                fixed_shared_memory_result::
                    not_found &&
            !missing.valid(),
            "owner reset removes shared-memory name");

        completed = true;
    }

    tests.expect(
        completed,
        "find exact address usable by both parent and child");

    const auto address =
        reserve_candidate(
            size);

    if (address != 0 &&
        address %
            address_alignment == 0) {

        fixed_shared_memory invalid;

        tests.expect(
            invalid.create(
                {},
                size,
                address) ==
                fixed_shared_memory_result::
                    invalid_argument &&
            invalid.create(
                "bad/name",
                size,
                address) ==
                fixed_shared_memory_result::
                    invalid_argument &&
            invalid.create(
                "bad-address",
                size,
                address + 1) ==
                fixed_shared_memory_result::
                    invalid_argument &&
            invalid.create(
                "bad-size",
                size - 1,
                address) ==
                fixed_shared_memory_result::
                    invalid_argument,
            "reject non-portable name and unaligned FIXED_DIRECT request");
    }
}

}
}

int main(
    int argc,
    char** argv) {

    using namespace cw::server;

    if (argc == 5 &&
        std::string_view{
            argv[1]} ==
            "--child") {

        return child_mode(
            argv[2],
            argv[3],
            argv[4]);
    }

    test_state tests;

    test_contract(
        tests,
        argc > 0
            ? argv[0]
            : "");

    if (tests.failures != 0) {
        std::cerr
            << tests.failures
            << " fixed_shared_memory test(s) failed\n";

        return 1;
    }

    return 0;
}
