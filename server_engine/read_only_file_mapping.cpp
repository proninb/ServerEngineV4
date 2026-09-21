#include "read_only_file_mapping.hpp"

#include <limits>
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

read_only_file_mapping::~read_only_file_mapping() {
    reset();
}

read_only_file_mapping::read_only_file_mapping(
    read_only_file_mapping&& other) noexcept
    : file_handle(
          std::exchange(
              other.file_handle,
              -1)),
      mapping_handle(
          std::exchange(
              other.mapping_handle,
              0)),
      address(
          std::exchange(
              other.address,
              nullptr)),
      length(
          std::exchange(
              other.length,
              0)) {
}

read_only_file_mapping&
read_only_file_mapping::operator=(
    read_only_file_mapping&& other) noexcept {

    if (this == &other) {
        return *this;
    }

    reset();

    file_handle =
        std::exchange(
            other.file_handle,
            -1);

    mapping_handle =
        std::exchange(
            other.mapping_handle,
            0);

    address =
        std::exchange(
            other.address,
            nullptr);

    length =
        std::exchange(
            other.length,
            0);

    return *this;
}

void read_only_file_mapping::reset() noexcept {
#if defined(_WIN32)
    if (address != nullptr) {
        UnmapViewOfFile(
            address);
    }

    if (mapping_handle != 0) {
        CloseHandle(
            reinterpret_cast<HANDLE>(
                mapping_handle));
    }

    if (file_handle != -1) {
        CloseHandle(
            reinterpret_cast<HANDLE>(
                file_handle));
    }
#else
    if (address != nullptr &&
        length != 0) {

        munmap(
            const_cast<std::byte*>(
                address),
            length);
    }

    if (file_handle >= 0) {
        close(
            static_cast<int>(
                file_handle));
    }
#endif

    file_handle = -1;
    mapping_handle = 0;
    address = nullptr;
    length = 0;
}

read_only_file_mapping_result
read_only_file_mapping::open(
    const std::filesystem::path& path) noexcept {

    reset();

    if (path.empty()) {
        return read_only_file_mapping_result::
            failed;
    }

#if defined(_WIN32)
    const auto file =
        CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
                FILE_FLAG_RANDOM_ACCESS,
            nullptr);

    if (file ==
        INVALID_HANDLE_VALUE) {

        const auto error =
            GetLastError();

        return error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND
            ? read_only_file_mapping_result::
                not_found
            : read_only_file_mapping_result::
                failed;
    }

    LARGE_INTEGER native_size{};

    if (GetFileSizeEx(
            file,
            &native_size) == 0) {

        CloseHandle(file);
        return read_only_file_mapping_result::
            failed;
    }

    if (native_size.QuadPart == 0) {
        CloseHandle(file);
        return read_only_file_mapping_result::
            empty;
    }

    if (native_size.QuadPart < 0 ||
        static_cast<unsigned long long>(
            native_size.QuadPart) >
            static_cast<unsigned long long>(
                (std::numeric_limits<
                    std::size_t>::max)())) {

        CloseHandle(file);
        return read_only_file_mapping_result::
            failed;
    }

    const auto mapping =
        CreateFileMappingW(
            file,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr);

    if (mapping == nullptr) {
        CloseHandle(file);
        return read_only_file_mapping_result::
            failed;
    }

    const auto view =
        MapViewOfFile(
            mapping,
            FILE_MAP_READ,
            0,
            0,
            0);

    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        return read_only_file_mapping_result::
            failed;
    }

    file_handle =
        reinterpret_cast<
            std::intptr_t>(
                file);

    mapping_handle =
        reinterpret_cast<
            std::intptr_t>(
                mapping);

    address =
        static_cast<
            const std::byte*>(
                view);

    length =
        static_cast<std::size_t>(
            native_size.QuadPart);

    return read_only_file_mapping_result::
        success;
#else
    const auto file =
        ::open(
            path.c_str(),
            O_RDONLY |
                O_CLOEXEC);

    if (file < 0) {
        return errno == ENOENT ||
            errno == ENOTDIR
            ? read_only_file_mapping_result::
                not_found
            : read_only_file_mapping_result::
                failed;
    }

    struct stat state{};

    if (fstat(
            file,
            &state) != 0 ||
        !S_ISREG(
            state.st_mode)) {

        close(file);
        return read_only_file_mapping_result::
            failed;
    }

    if (state.st_size == 0) {
        close(file);
        return read_only_file_mapping_result::
            empty;
    }

    if (state.st_size < 0 ||
        static_cast<std::uintmax_t>(
            state.st_size) >
            static_cast<std::uintmax_t>(
                (std::numeric_limits<
                    std::size_t>::max)())) {

        close(file);
        return read_only_file_mapping_result::
            failed;
    }

    const auto size =
        static_cast<std::size_t>(
            state.st_size);

    const auto view =
        mmap(
            nullptr,
            size,
            PROT_READ,
            MAP_PRIVATE,
            file,
            0);

    if (view == MAP_FAILED) {
        close(file);
        return read_only_file_mapping_result::
            failed;
    }

    file_handle = file;
    mapping_handle = 0;

    address =
        static_cast<
            const std::byte*>(
                view);

    length = size;

    return read_only_file_mapping_result::
        success;
#endif
}

}
