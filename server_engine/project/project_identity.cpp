#include "project_identity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
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
#include <winioctl.h>
#endif

namespace cw::server {
namespace {

[[nodiscard]] constexpr std::uint32_t rotr(
    std::uint32_t value,
    std::uint32_t amount) noexcept {

    return (value >> amount) |
        (value << (32U - amount));
}

[[nodiscard]] constexpr std::uint32_t choose(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {

    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {

    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t big_sigma0(
    std::uint32_t value) noexcept {

    return rotr(value, 2) ^
        rotr(value, 13) ^
        rotr(value, 22);
}

[[nodiscard]] constexpr std::uint32_t big_sigma1(
    std::uint32_t value) noexcept {

    return rotr(value, 6) ^
        rotr(value, 11) ^
        rotr(value, 25);
}

[[nodiscard]] constexpr std::uint32_t small_sigma0(
    std::uint32_t value) noexcept {

    return rotr(value, 7) ^
        rotr(value, 18) ^
        (value >> 3);
}

[[nodiscard]] constexpr std::uint32_t small_sigma1(
    std::uint32_t value) noexcept {

    return rotr(value, 17) ^
        rotr(value, 19) ^
        (value >> 10);
}

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4U, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

class sha256_state final {
public:
    void update(std::string_view input) noexcept {
        for (const char character : input) {
            buffer[buffer_size++] =
                static_cast<unsigned char>(character);
            ++total_bytes;

            if (buffer_size == buffer.size()) {
                transform(buffer.data());
                buffer_size = 0;
            }
        }
    }

    [[nodiscard]] project_content_hash finish() noexcept {
        const auto bit_length =
            total_bytes * 8ULL;

        buffer[buffer_size++] = 0x80U;

        if (buffer_size > 56) {
            while (buffer_size < 64) {
                buffer[buffer_size++] = 0;
            }

            transform(buffer.data());
            buffer_size = 0;
        }

        while (buffer_size < 56) {
            buffer[buffer_size++] = 0;
        }

        for (std::size_t index = 0;
             index < 8;
             ++index) {

            const auto shift =
                static_cast<unsigned>(
                    (7 - index) * 8);

            buffer[buffer_size++] =
                static_cast<std::uint8_t>(
                    bit_length >> shift);
        }

        transform(buffer.data());

        project_content_hash output;

        for (std::size_t word = 0;
             word < state.size();
             ++word) {

            for (std::size_t byte = 0;
                 byte < 4;
                 ++byte) {

                const auto shift =
                    static_cast<unsigned>(
                        (3 - byte) * 8);

                output.bytes[word * 4 + byte] =
                    static_cast<std::byte>(
                        (state[word] >> shift) &
                        0xffU);
            }
        }

        return output;
    }

private:
    void transform(
        const std::uint8_t* block) noexcept {

        std::array<std::uint32_t, 64> schedule{};

        for (std::size_t index = 0;
             index < 16;
             ++index) {

            const auto offset =
                index * 4;

            schedule[index] =
                (static_cast<std::uint32_t>(
                    block[offset]) << 24) |
                (static_cast<std::uint32_t>(
                    block[offset + 1]) << 16) |
                (static_cast<std::uint32_t>(
                    block[offset + 2]) << 8) |
                static_cast<std::uint32_t>(
                    block[offset + 3]);
        }

        for (std::size_t index = 16;
             index < schedule.size();
             ++index) {

            schedule[index] =
                small_sigma1(
                    schedule[index - 2]) +
                schedule[index - 7] +
                small_sigma0(
                    schedule[index - 15]) +
                schedule[index - 16];
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];

        for (std::size_t index = 0;
             index < schedule.size();
             ++index) {

            const auto temporary1 =
                h +
                big_sigma1(e) +
                choose(e, f, g) +
                round_constants[index] +
                schedule[index];

            const auto temporary2 =
                big_sigma0(a) +
                majority(a, b, c);

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<std::uint32_t, 8> state{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };

    std::array<std::uint8_t, 64> buffer{};
    std::size_t buffer_size = 0;
    std::uint64_t total_bytes = 0;
};

[[nodiscard]] bool observe(
    const std::filesystem::path& path,
    file_snapshot_observation& output,
    bool& missing) noexcept {

    output = {};
    missing = false;

    std::error_code error;

    const auto status =
        std::filesystem::status(
            path,
            error);

    if (error) {
        if (error ==
            std::errc::no_such_file_or_directory) {

            missing = true;
            return true;
        }

        return false;
    }

    if (!std::filesystem::exists(status)) {
        missing = true;
        return true;
    }

    if (!std::filesystem::is_regular_file(status)) {
        return false;
    }

    const auto size =
        std::filesystem::file_size(
            path,
            error);

    if (error) {
        return false;
    }

    const auto write_time =
        std::filesystem::last_write_time(
            path,
            error);

    if (error) {
        return false;
    }

    output.size = size;
    output.write_time_ticks =
        static_cast<std::int64_t>(
            write_time.time_since_epoch().count());

    return true;
}

#if defined(_WIN32)

class windows_handle final {
public:
    explicit windows_handle(
        HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : handle(value) {
    }

    windows_handle(
        const windows_handle&) = delete;

    windows_handle& operator=(
        const windows_handle&) = delete;

    windows_handle(
        windows_handle&& other) noexcept
        : handle(
            std::exchange(
                other.handle,
                INVALID_HANDLE_VALUE)) {
    }

    ~windows_handle() {
        if (*this) {
            CloseHandle(handle);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle != INVALID_HANDLE_VALUE &&
            handle != nullptr;
    }

private:
    HANDLE handle = INVALID_HANDLE_VALUE;
};

struct observed_file_change_state final {
    std::uint64_t volume_serial = 0;
    std::uint64_t file_reference = 0;
    std::int64_t file_usn = -1;

    [[nodiscard]] explicit operator bool() const noexcept {
        return volume_serial != 0 &&
            file_reference != 0 &&
            file_usn >= 0;
    }
};

[[nodiscard]] project_token_result query_file_change_state(
    const std::filesystem::path& path,
    observed_file_change_state& output) noexcept {

    output = {};

    try {
        const auto native =
            path.wstring();

        windows_handle file{
            CreateFileW(
                native.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ |
                    FILE_SHARE_WRITE |
                    FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr)};

        if (!file) {
            const auto error =
                GetLastError();

            if (error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PATH_NOT_FOUND) {

                return project_token_result::missing;
            }

            return project_token_result::failed;
        }

        BY_HANDLE_FILE_INFORMATION information{};

        if (GetFileInformationByHandle(
                file.get(),
                &information) == 0) {

            return project_token_result::failed;
        }

        READ_FILE_USN_DATA request{};
        request.MinMajorVersion = 2;
        request.MaxMajorVersion = 2;

        std::array<std::byte, 4096> buffer{};
        DWORD returned = 0;

        if (DeviceIoControl(
                file.get(),
                FSCTL_READ_FILE_USN_DATA,
                &request,
                sizeof(request),
                buffer.data(),
                static_cast<DWORD>(
                    buffer.size()),
                &returned,
                nullptr) == 0) {

            const auto error =
                GetLastError();

            if (error == ERROR_INVALID_FUNCTION ||
                error == ERROR_NOT_SUPPORTED ||
                error == ERROR_INVALID_PARAMETER) {

                return project_token_result::unavailable;
            }

            return project_token_result::failed;
        }

        if (returned < sizeof(USN_RECORD_V2)) {
            return project_token_result::failed;
        }

        USN_RECORD_V2 record{};

        std::memcpy(
            &record,
            buffer.data(),
            sizeof(record));

        const auto file_reference =
            (static_cast<std::uint64_t>(
                information.nFileIndexHigh) << 32) |
            information.nFileIndexLow;

        if (record.MajorVersion != 2 ||
            record.RecordLength <
                sizeof(USN_RECORD_V2) ||
            record.RecordLength > returned ||
            record.Usn < 0 ||
            file_reference == 0 ||
            record.FileReferenceNumber !=
                file_reference) {

            return project_token_result::failed;
        }

        output.volume_serial =
            information.dwVolumeSerialNumber;

        output.file_reference =
            file_reference;

        output.file_usn =
            static_cast<std::int64_t>(
                record.Usn);

        return output
            ? project_token_result::available
            : project_token_result::unavailable;
    }
    catch (...) {
        return project_token_result::failed;
    }
}

[[nodiscard]] project_snapshot_result
acquire_windows_snapshot(
    const std::filesystem::path& path,
    project_content_snapshot& output) noexcept {

    output = {};

    try {
        windows_handle file{
            CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL |
                    FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr)};

        if (!file) {
            const auto error =
                GetLastError();

            if (error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PATH_NOT_FOUND) {

                return project_snapshot_result::missing;
            }

            if (error == ERROR_SHARING_VIOLATION ||
                error == ERROR_LOCK_VIOLATION) {

                return project_snapshot_result::
                    changed_during_read;
            }

            return project_snapshot_result::failed;
        }

        BY_HANDLE_FILE_INFORMATION before{};

        if (GetFileInformationByHandle(
                file.get(),
                &before) == 0) {

            return project_snapshot_result::failed;
        }

        const auto native_size =
            (static_cast<std::uint64_t>(
                before.nFileSizeHigh) << 32) |
            before.nFileSizeLow;

        if (native_size >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return project_snapshot_result::failed;
        }

        std::string bytes(
            static_cast<std::size_t>(
                native_size),
            '\0');

        std::size_t offset = 0;

        while (offset < bytes.size()) {
            const auto remaining =
                bytes.size() - offset;

            constexpr std::size_t maximum_chunk =
                1024u * 1024u * 1024u;

            const auto chunk =
                static_cast<DWORD>(
                    (std::min<std::size_t>)(
                        remaining,
                        maximum_chunk));

            DWORD read = 0;

            if (ReadFile(
                    file.get(),
                    bytes.data() + offset,
                    chunk,
                    &read,
                    nullptr) == 0 ||
                read != chunk) {

                return project_snapshot_result::
                    changed_during_read;
            }

            offset +=
                static_cast<std::size_t>(read);
        }

        BY_HANDLE_FILE_INFORMATION after{};

        if (GetFileInformationByHandle(
                file.get(),
                &after) == 0) {

            return project_snapshot_result::failed;
        }

        const bool same =
            before.dwVolumeSerialNumber ==
                after.dwVolumeSerialNumber &&
            before.nFileIndexHigh ==
                after.nFileIndexHigh &&
            before.nFileIndexLow ==
                after.nFileIndexLow &&
            before.nFileSizeHigh ==
                after.nFileSizeHigh &&
            before.nFileSizeLow ==
                after.nFileSizeLow &&
            before.ftLastWriteTime.dwHighDateTime ==
                after.ftLastWriteTime.dwHighDateTime &&
            before.ftLastWriteTime.dwLowDateTime ==
                after.ftLastWriteTime.dwLowDateTime;

        if (!same) {
            return project_snapshot_result::
                changed_during_read;
        }

        std::error_code error;

        const auto write_time =
            std::filesystem::last_write_time(
                path,
                error);

        if (error) {
            return project_snapshot_result::failed;
        }

        output.observation.size =
            native_size;

        output.observation.write_time_ticks =
            static_cast<std::int64_t>(
                write_time.time_since_epoch().count());

        output.content_hash =
            hash_project_content(bytes);

        output.bytes =
            std::move(bytes);

        observed_file_change_state state;

        if (query_file_change_state(
                path,
                state) ==
            project_token_result::available) {

            output.change_token = {
                state.volume_serial,
                state.file_reference,
                state.file_usn,
            };

            output.change_token_available = true;
        }

        return project_snapshot_result::acquired;
    }
    catch (const std::bad_alloc&) {
        return project_snapshot_result::
            allocation_failed;
    }
    catch (const std::length_error&) {
        return project_snapshot_result::
            allocation_failed;
    }
    catch (...) {
        return project_snapshot_result::failed;
    }
}

#endif

} // namespace

project_content_hash hash_project_content(
    std::string_view bytes) noexcept {

    sha256_state hash;
    hash.update(bytes);
    return hash.finish();
}

project_token_result capture_file_change_token(
    const std::filesystem::path& path,
    file_change_token& output) noexcept {

    output = {};

#if !defined(_WIN32)
    (void)path;
    return project_token_result::unavailable;
#else
    observed_file_change_state state;

    const auto result =
        query_file_change_state(
            path,
            state);

    if (result != project_token_result::available) {
        return result;
    }

    output = {
        state.volume_serial,
        state.file_reference,
        state.file_usn,
    };

    return project_token_result::available;
#endif
}

project_token_result prove_file_unchanged(
    const std::filesystem::path& path,
    const file_change_token& token,
    bool& unchanged) noexcept {

    unchanged = false;

    if (!token) {
        return project_token_result::unavailable;
    }

#if !defined(_WIN32)
    (void)path;
    return project_token_result::unavailable;
#else
    observed_file_change_state current;

    const auto result =
        query_file_change_state(
            path,
            current);

    if (result != project_token_result::available) {
        return result;
    }

    unchanged =
        current.volume_serial ==
            token.volume_serial &&
        current.file_reference ==
            token.file_reference &&
        current.file_usn ==
            token.file_usn;

    return project_token_result::available;
#endif
}

project_snapshot_result acquire_project_content(
    const std::filesystem::path& path,
    project_content_snapshot& output) noexcept {

#if defined(_WIN32)
    return acquire_windows_snapshot(
        path,
        output);
#else
    output = {};

    file_snapshot_observation before;
    bool missing = false;

    if (!observe(
            path,
            before,
            missing)) {

        return project_snapshot_result::failed;
    }

    if (missing) {
        return project_snapshot_result::missing;
    }

    if (before.size >
        (std::numeric_limits<std::uint32_t>::max)()) {

        return project_snapshot_result::failed;
    }

    try {
        std::ifstream stream(
            path,
            std::ios::binary);

        if (!stream) {
            return project_snapshot_result::failed;
        }

        std::string bytes(
            static_cast<std::size_t>(
                before.size),
            '\0');

        if (!bytes.empty()) {
            stream.read(
                bytes.data(),
                static_cast<std::streamsize>(
                    bytes.size()));

            if (stream.gcount() !=
                static_cast<std::streamsize>(
                    bytes.size())) {

                return project_snapshot_result::
                    changed_during_read;
            }
        }

        file_snapshot_observation after;

        if (!observe(
                path,
                after,
                missing)) {

            return project_snapshot_result::failed;
        }

        if (missing ||
            before != after) {

            return project_snapshot_result::
                changed_during_read;
        }

        output.observation = after;
        output.content_hash =
            hash_project_content(bytes);
        output.bytes =
            std::move(bytes);

        return project_snapshot_result::acquired;
    }
    catch (const std::bad_alloc&) {
        return project_snapshot_result::
            allocation_failed;
    }
    catch (const std::length_error&) {
        return project_snapshot_result::
            allocation_failed;
    }
#endif
}

}
