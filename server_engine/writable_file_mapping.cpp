#include "writable_file_mapping.hpp"

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
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace cw::server {

writable_file_mapping::~writable_file_mapping() {
    reset();
}

writable_file_mapping::writable_file_mapping(
    writable_file_mapping&& other) noexcept
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

writable_file_mapping&
writable_file_mapping::operator=(
    writable_file_mapping&& other) noexcept {

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

void writable_file_mapping::reset() noexcept {
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
            address,
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

writable_file_mapping_result
writable_file_mapping::create(
    const std::filesystem::path& path,
    std::size_t size) noexcept {

    reset();

    if (path.empty() ||
        size == 0) {

        return writable_file_mapping_result::
            failed;
    }

#if defined(_WIN32)
    if (size >
        static_cast<std::size_t>(
            (std::numeric_limits<LONGLONG>::max)())) {

        return writable_file_mapping_result::
            failed;
    }

    const auto file =
        CreateFileW(
            path.c_str(),
            GENERIC_READ |
                GENERIC_WRITE,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL |
                FILE_FLAG_RANDOM_ACCESS,
            nullptr);

    if (file ==
        INVALID_HANDLE_VALUE) {

        return writable_file_mapping_result::
            failed;
    }

    LARGE_INTEGER end{};
    end.QuadPart =
        static_cast<LONGLONG>(
            size);

    if (SetFilePointerEx(
            file,
            end,
            nullptr,
            FILE_BEGIN) == 0 ||
        SetEndOfFile(
            file) == 0) {

        CloseHandle(file);
        return writable_file_mapping_result::
            failed;
    }

    const auto mapping =
        CreateFileMappingW(
            file,
            nullptr,
            PAGE_READWRITE,
            0,
            0,
            nullptr);

    if (mapping == nullptr) {
        CloseHandle(file);
        return writable_file_mapping_result::
            failed;
    }

    const auto view =
        MapViewOfFile(
            mapping,
            FILE_MAP_READ |
                FILE_MAP_WRITE,
            0,
            0,
            0);

    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        return writable_file_mapping_result::
            failed;
    }

    file_handle =
        reinterpret_cast<std::intptr_t>(
            file);

    mapping_handle =
        reinterpret_cast<std::intptr_t>(
            mapping);

    address =
        static_cast<std::byte*>(
            view);

    length = size;

    return writable_file_mapping_result::
        success;
#else
    if (size >
        static_cast<std::size_t>(
            (std::numeric_limits<off_t>::max)())) {

        return writable_file_mapping_result::
            failed;
    }

    const auto file =
        ::open(
            path.c_str(),
            O_RDWR |
                O_CREAT |
                O_TRUNC |
                O_CLOEXEC,
            0666);

    if (file < 0) {
        return writable_file_mapping_result::
            failed;
    }

    if (ftruncate(
            file,
            static_cast<off_t>(
                size)) != 0) {

        close(file);
        return writable_file_mapping_result::
            failed;
    }

    const auto view =
        mmap(
            nullptr,
            size,
            PROT_READ |
                PROT_WRITE,
            MAP_SHARED,
            file,
            0);

    if (view == MAP_FAILED) {
        close(file);
        return writable_file_mapping_result::
            failed;
    }

    file_handle = file;
    mapping_handle = 0;
    address =
        static_cast<std::byte*>(
            view);
    length = size;

    return writable_file_mapping_result::
        success;
#endif
}

writable_file_mapping_result
writable_file_mapping::flush() noexcept {

    if (!valid()) {
        return writable_file_mapping_result::
            failed;
    }

#if defined(_WIN32)
    if (FlushViewOfFile(
            address,
            length) == 0 ||
        FlushFileBuffers(
            reinterpret_cast<HANDLE>(
                file_handle)) == 0) {

        return writable_file_mapping_result::
            failed;
    }
#else
    if (msync(
            address,
            length,
            MS_SYNC) != 0 ||
        fsync(
            static_cast<int>(
                file_handle)) != 0) {

        return writable_file_mapping_result::
            failed;
    }
#endif

    return writable_file_mapping_result::
        success;
}

}
