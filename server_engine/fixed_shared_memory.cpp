#include "fixed_shared_memory.hpp"

#include "shared_memory_name.hpp"

#include <limits>
#include <string>
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
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cw::server {
namespace {

[[nodiscard]] bool valid_request(
    std::string_view name,
    std::size_t size,
    std::uintptr_t fixed_address) noexcept {

    const auto address_alignment =
        fixed_shared_memory::
            address_alignment();

    const auto size_alignment =
        fixed_shared_memory::
            size_alignment();

    return valid_shared_memory_name(name) &&
        size != 0 &&
        fixed_address != 0 &&
        address_alignment != 0 &&
        size_alignment != 0 &&
        fixed_address %
            address_alignment == 0 &&
        size %
            size_alignment == 0;
}

#if defined(_WIN32)

[[nodiscard]] bool make_native_name(
    std::string_view name,
    std::wstring& output) noexcept {

    output.clear();

    try {
        output.reserve(
            name.size());

        for (const auto value : name) {
            output.push_back(
                static_cast<wchar_t>(
                    static_cast<unsigned char>(
                        value)));
        }

        return true;
    }
    catch (...) {
        output.clear();
        return false;
    }
}

[[nodiscard]] fixed_shared_memory_result
validate_mapped_size(
    void* address,
    std::size_t expected) noexcept {

    MEMORY_BASIC_INFORMATION information{};

    if (VirtualQuery(
            address,
            &information,
            sizeof(information)) !=
            sizeof(information) ||
        information.AllocationBase !=
            address ||
        information.RegionSize !=
            expected) {

        return fixed_shared_memory_result::
            size_mismatch;
    }

    return fixed_shared_memory_result::
        success;
}

#else

[[nodiscard]] bool make_native_name(
    std::string_view name,
    std::string& output) noexcept {

    output.clear();

    try {
        output.reserve(
            name.size() + 1);

        output.push_back('/');
        output.append(name);
        return true;
    }
    catch (...) {
        output.clear();
        return false;
    }
}

[[nodiscard]] int open_flags(
    int base) noexcept {

#ifdef O_CLOEXEC
    return base | O_CLOEXEC;
#else
    return base;
#endif
}

#endif

}

fixed_shared_memory::~fixed_shared_memory() {
    reset();
}

fixed_shared_memory::fixed_shared_memory(
    fixed_shared_memory&& other) noexcept
    : native_handle(
          std::exchange(
              other.native_handle,
              0)),
      address_value(
          std::exchange(
              other.address_value,
              nullptr)),
      length(
          std::exchange(
              other.length,
              0)),
      native_name(
          std::move(
              other.native_name)),
      owns_name(
          std::exchange(
              other.owns_name,
              false)) {
}

fixed_shared_memory&
fixed_shared_memory::operator=(
    fixed_shared_memory&& other) noexcept {

    if (this == &other) {
        return *this;
    }

    reset();

    native_handle =
        std::exchange(
            other.native_handle,
            0);

    address_value =
        std::exchange(
            other.address_value,
            nullptr);

    length =
        std::exchange(
            other.length,
            0);

    native_name =
        std::move(
            other.native_name);

    owns_name =
        std::exchange(
            other.owns_name,
            false);

    return *this;
}

std::size_t
fixed_shared_memory::address_alignment() noexcept {
#if defined(_WIN32)
    SYSTEM_INFO information{};
    GetSystemInfo(
        &information);

    return static_cast<std::size_t>(
        information.dwAllocationGranularity);
#else
    const auto value =
        sysconf(
            _SC_PAGESIZE);

    return value > 0
        ? static_cast<std::size_t>(
            value)
        : 0;
#endif
}

std::size_t
fixed_shared_memory::size_alignment() noexcept {
#if defined(_WIN32)
    SYSTEM_INFO information{};
    GetSystemInfo(
        &information);

    return static_cast<std::size_t>(
        information.dwPageSize);
#else
    const auto value =
        sysconf(
            _SC_PAGESIZE);

    return value > 0
        ? static_cast<std::size_t>(
            value)
        : 0;
#endif
}

void fixed_shared_memory::reset() noexcept {
#if defined(_WIN32)
    if (address_value != nullptr) {
        UnmapViewOfFile(
            address_value);
    }

    if (native_handle != 0) {
        CloseHandle(
            reinterpret_cast<HANDLE>(
                native_handle));
    }
#else
    if (address_value != nullptr &&
        length != 0) {

        munmap(
            address_value,
            length);
    }

    if (native_handle != 0) {
        close(
            static_cast<int>(
                native_handle - 1));
    }

    if (owns_name &&
        !native_name.empty()) {

        shm_unlink(
            native_name.c_str());
    }
#endif

    native_handle = 0;
    address_value = nullptr;
    length = 0;
    native_name.clear();
    owns_name = false;
}

fixed_shared_memory_result
fixed_shared_memory::create(
    std::string_view name,
    std::size_t size,
    std::uintptr_t fixed_address) noexcept {

    reset();

    if (!valid_request(
            name,
            size,
            fixed_address)) {

        return fixed_shared_memory_result::
            invalid_argument;
    }

#if defined(_WIN32)
    std::wstring native;

    if (!make_native_name(
            name,
            native)) {

        return fixed_shared_memory_result::
            failed;
    }

    ULARGE_INTEGER native_size{};
    native_size.QuadPart =
        static_cast<ULONGLONG>(
            size);

    const auto mapping =
        CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            native_size.HighPart,
            native_size.LowPart,
            native.c_str());

    if (mapping == nullptr) {
        return fixed_shared_memory_result::
            failed;
    }

    if (GetLastError() ==
        ERROR_ALREADY_EXISTS) {

        CloseHandle(mapping);
        return fixed_shared_memory_result::
            already_exists;
    }

    auto* requested =
        reinterpret_cast<void*>(
            fixed_address);

    auto* view =
        MapViewOfFileEx(
            mapping,
            FILE_MAP_READ |
                FILE_MAP_WRITE,
            0,
            0,
            0,
            requested);

    if (view == nullptr) {
        const auto error =
            GetLastError();

        CloseHandle(mapping);

        return error ==
                ERROR_INVALID_ADDRESS ||
            error ==
                ERROR_MAPPED_ALIGNMENT
            ? fixed_shared_memory_result::
                address_unavailable
            : fixed_shared_memory_result::
                failed;
    }

    if (view != requested) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);

        return fixed_shared_memory_result::
            address_unavailable;
    }

    const auto size_result =
        validate_mapped_size(
            view,
            size);

    if (size_result !=
        fixed_shared_memory_result::
            success) {

        UnmapViewOfFile(view);
        CloseHandle(mapping);
        return size_result;
    }

    native_handle =
        reinterpret_cast<std::intptr_t>(
            mapping);

    address_value =
        static_cast<std::byte*>(
            view);

    length = size;

    return fixed_shared_memory_result::
        success;
#else
    std::string native;

    if (!make_native_name(
            name,
            native)) {

        return fixed_shared_memory_result::
            failed;
    }

    if (size >
        static_cast<std::size_t>(
            (std::numeric_limits<off_t>::max)())) {

        return fixed_shared_memory_result::
            invalid_argument;
    }

    const auto file =
        shm_open(
            native.c_str(),
            open_flags(
                O_RDWR |
                O_CREAT |
                O_EXCL),
            0600);

    if (file < 0) {
        return errno == EEXIST
            ? fixed_shared_memory_result::
                already_exists
            : fixed_shared_memory_result::
                failed;
    }

    if (ftruncate(
            file,
            static_cast<off_t>(
                size)) != 0) {

        close(file);
        shm_unlink(
            native.c_str());

        return fixed_shared_memory_result::
            failed;
    }

    auto* requested =
        reinterpret_cast<void*>(
            fixed_address);

    auto flags =
        MAP_SHARED;

#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#endif

    auto* view =
        mmap(
            requested,
            size,
            PROT_READ |
                PROT_WRITE,
            flags,
            file,
            0);

    if (view == MAP_FAILED) {
        const auto error = errno;

        close(file);
        shm_unlink(
            native.c_str());

        return error == EEXIST
            ? fixed_shared_memory_result::
                address_unavailable
            : fixed_shared_memory_result::
                failed;
    }

    if (view != requested) {
        munmap(
            view,
            size);

        close(file);
        shm_unlink(
            native.c_str());

        return fixed_shared_memory_result::
            address_unavailable;
    }

    native_handle =
        static_cast<std::intptr_t>(
            file) +
        1;

    address_value =
        static_cast<std::byte*>(
            view);

    length = size;
    native_name =
        std::move(native);

    owns_name = true;

    return fixed_shared_memory_result::
        success;
#endif
}

fixed_shared_memory_result
fixed_shared_memory::open(
    std::string_view name,
    std::size_t size,
    std::uintptr_t fixed_address) noexcept {

    reset();

    if (!valid_request(
            name,
            size,
            fixed_address)) {

        return fixed_shared_memory_result::
            invalid_argument;
    }

#if defined(_WIN32)
    std::wstring native;

    if (!make_native_name(
            name,
            native)) {

        return fixed_shared_memory_result::
            failed;
    }

    const auto mapping =
        OpenFileMappingW(
            FILE_MAP_READ |
                FILE_MAP_WRITE,
            FALSE,
            native.c_str());

    if (mapping == nullptr) {
        return GetLastError() ==
                ERROR_FILE_NOT_FOUND
            ? fixed_shared_memory_result::
                not_found
            : fixed_shared_memory_result::
                failed;
    }

    auto* requested =
        reinterpret_cast<void*>(
            fixed_address);

    auto* view =
        MapViewOfFileEx(
            mapping,
            FILE_MAP_READ |
                FILE_MAP_WRITE,
            0,
            0,
            0,
            requested);

    if (view == nullptr) {
        const auto error =
            GetLastError();

        CloseHandle(mapping);

        return error ==
                ERROR_INVALID_ADDRESS ||
            error ==
                ERROR_MAPPED_ALIGNMENT
            ? fixed_shared_memory_result::
                address_unavailable
            : fixed_shared_memory_result::
                failed;
    }

    if (view != requested) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);

        return fixed_shared_memory_result::
            address_unavailable;
    }

    const auto size_result =
        validate_mapped_size(
            view,
            size);

    if (size_result !=
        fixed_shared_memory_result::
            success) {

        UnmapViewOfFile(view);
        CloseHandle(mapping);
        return size_result;
    }

    native_handle =
        reinterpret_cast<std::intptr_t>(
            mapping);

    address_value =
        static_cast<std::byte*>(
            view);

    length = size;

    return fixed_shared_memory_result::
        success;
#else
    std::string native;

    if (!make_native_name(
            name,
            native)) {

        return fixed_shared_memory_result::
            failed;
    }

    const auto file =
        shm_open(
            native.c_str(),
            open_flags(O_RDWR),
            0600);

    if (file < 0) {
        return errno == ENOENT
            ? fixed_shared_memory_result::
                not_found
            : fixed_shared_memory_result::
                failed;
    }

    struct stat state{};

    if (fstat(
            file,
            &state) != 0 ||
        state.st_size < 0) {

        close(file);
        return fixed_shared_memory_result::
            failed;
    }

    if (static_cast<std::uintmax_t>(
            state.st_size) !=
        static_cast<std::uintmax_t>(
            size)) {

        close(file);
        return fixed_shared_memory_result::
            size_mismatch;
    }

    auto* requested =
        reinterpret_cast<void*>(
            fixed_address);

    auto flags =
        MAP_SHARED;

#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#endif

    auto* view =
        mmap(
            requested,
            size,
            PROT_READ |
                PROT_WRITE,
            flags,
            file,
            0);

    if (view == MAP_FAILED) {
        const auto error = errno;

        close(file);

        return error == EEXIST
            ? fixed_shared_memory_result::
                address_unavailable
            : fixed_shared_memory_result::
                failed;
    }

    if (view != requested) {
        munmap(
            view,
            size);

        close(file);

        return fixed_shared_memory_result::
            address_unavailable;
    }

    native_handle =
        static_cast<std::intptr_t>(
            file) +
        1;

    address_value =
        static_cast<std::byte*>(
            view);

    length = size;
    native_name =
        std::move(native);

    owns_name = false;

    return fixed_shared_memory_result::
        success;
#endif
}

}
