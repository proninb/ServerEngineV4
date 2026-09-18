#include "project_identity_store.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
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
#endif

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'C'},
    std::byte{'W'},
    std::byte{'P'},
    std::byte{'I'},
    std::byte{'D'},
    std::byte{'0'},
    std::byte{'0'},
    std::byte{'1'},
};

constexpr std::uint32_t format_version = 1;

constexpr std::uint32_t content_hash_flag = 0x01u;
constexpr std::uint32_t semantic_fingerprint_flag = 0x02u;
constexpr std::uint32_t change_token_flag = 0x04u;
constexpr std::uint32_t known_flags =
    content_hash_flag |
    semantic_fingerprint_flag |
    change_token_flag;

constexpr std::size_t content_hash_offset = 16;
constexpr std::size_t semantic_fingerprint_offset = 48;
constexpr std::size_t change_token_offset = 80;
constexpr std::size_t checksum_offset = 104;
constexpr std::size_t file_size = 136;

using image = std::array<std::byte, file_size>;

void write_u32(
    std::byte* target,
    std::uint32_t value) noexcept {

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

void write_u64(
    std::byte* target,
    std::uint64_t value) noexcept {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

[[nodiscard]] std::uint32_t read_u32(
    const std::byte* source) noexcept {

    std::uint32_t value = 0;

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    source[index]))
            << (index * 8);
    }

    return value;
}

[[nodiscard]] std::uint64_t read_u64(
    const std::byte* source) noexcept {

    std::uint64_t value = 0;

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    source[index]))
            << (index * 8);
    }

    return value;
}

[[nodiscard]] project_content_hash checksum(
    const image& value) noexcept {

    return hash_project_content(
        std::string_view{
            reinterpret_cast<const char*>(
                value.data()),
            checksum_offset});
}

[[nodiscard]] bool all_zero(
    const std::byte* begin,
    std::size_t size) noexcept {

    for (std::size_t index = 0;
         index < size;
         ++index) {

        if (begin[index] != std::byte{0}) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool encode(
    const persisted_project_identity& identity,
    image& output) noexcept {

    output = {};

    std::copy(
        magic.begin(),
        magic.end(),
        output.begin());

    write_u32(
        output.data() + 8,
        format_version);

    std::uint32_t flags = 0;

    if (identity.content_hash_available) {
        flags |= content_hash_flag;

        std::copy(
            identity.content_hash.bytes.begin(),
            identity.content_hash.bytes.end(),
            output.begin() +
                content_hash_offset);
    }

    if (identity.semantic_fingerprint_available) {
        flags |= semantic_fingerprint_flag;

        std::copy(
            identity.semantic_fingerprint.bytes.begin(),
            identity.semantic_fingerprint.bytes.end(),
            output.begin() +
                semantic_fingerprint_offset);
    }

    if (identity.change_token_available) {
        if (!identity.change_token) {
            return false;
        }

        flags |= change_token_flag;

        write_u64(
            output.data() +
                change_token_offset,
            identity.change_token.volume_serial);

        write_u64(
            output.data() +
                change_token_offset + 8,
            identity.change_token.file_reference);

        write_u64(
            output.data() +
                change_token_offset + 16,
            static_cast<std::uint64_t>(
                identity.change_token.file_usn));
    }

    write_u32(
        output.data() + 12,
        flags);

    const auto digest =
        checksum(output);

    std::copy(
        digest.bytes.begin(),
        digest.bytes.end(),
        output.begin() +
            checksum_offset);

    return true;
}

[[nodiscard]] bool decode(
    const image& input,
    persisted_project_identity& output) noexcept {

    output = {};

    if (!std::equal(
            magic.begin(),
            magic.end(),
            input.begin())) {

        return false;
    }

    if (read_u32(
            input.data() + 8) !=
        format_version) {

        return false;
    }

    const auto flags =
        read_u32(
            input.data() + 12);

    if ((flags & ~known_flags) != 0) {
        return false;
    }

    const auto expected =
        checksum(input);

    if (!std::equal(
            expected.bytes.begin(),
            expected.bytes.end(),
            input.begin() +
                checksum_offset)) {

        return false;
    }

    output.content_hash_available =
        (flags & content_hash_flag) != 0;

    output.semantic_fingerprint_available =
        (flags &
         semantic_fingerprint_flag) != 0;

    output.change_token_available =
        (flags & change_token_flag) != 0;

    if (output.content_hash_available) {
        std::copy_n(
            input.begin() +
                content_hash_offset,
            output.content_hash.bytes.size(),
            output.content_hash.bytes.begin());
    } else if (!all_zero(
                   input.data() +
                       content_hash_offset,
                   32)) {

        return false;
    }

    if (output.semantic_fingerprint_available) {
        std::copy_n(
            input.begin() +
                semantic_fingerprint_offset,
            output.semantic_fingerprint.bytes.size(),
            output.semantic_fingerprint.bytes.begin());
    } else if (!all_zero(
                   input.data() +
                       semantic_fingerprint_offset,
                   32)) {

        return false;
    }

    if (output.change_token_available) {
        output.change_token.volume_serial =
            read_u64(
                input.data() +
                    change_token_offset);

        output.change_token.file_reference =
            read_u64(
                input.data() +
                    change_token_offset + 8);

        output.change_token.file_usn =
            static_cast<std::int64_t>(
                read_u64(
                    input.data() +
                        change_token_offset + 16));

        if (!output.change_token) {
            return false;
        }
    } else if (!all_zero(
                   input.data() +
                       change_token_offset,
                   24)) {

        return false;
    }

    return true;
}

[[nodiscard]] bool read_image(
    const std::filesystem::path& path,
    image& output,
    bool& missing) noexcept {

    missing = false;
    output = {};

    try {
        std::ifstream stream(
            path,
            std::ios::binary);

        if (!stream) {
            std::error_code error;

            if (!std::filesystem::exists(
                    path,
                    error) &&
                !error) {

                missing = true;
                return true;
            }

            return false;
        }

        stream.read(
            reinterpret_cast<char*>(
                output.data()),
            static_cast<std::streamsize>(
                output.size()));

        if (stream.gcount() !=
            static_cast<std::streamsize>(
                output.size())) {

            return false;
        }

        char extra = 0;

        if (stream.read(
                &extra,
                1)) {

            return false;
        }

        return stream.eof();
    }
    catch (...) {
        return false;
    }
}

[[nodiscard]] bool write_image(
    const std::filesystem::path& path,
    const image& value) noexcept {

    try {
        std::error_code error;

        std::filesystem::create_directories(
            path.parent_path(),
            error);

        if (error) {
            return false;
        }

        auto temporary = path;
        temporary += ".tmp";

        {
            std::ofstream stream(
                temporary,
                std::ios::binary |
                    std::ios::trunc);

            if (!stream) {
                return false;
            }

            stream.write(
                reinterpret_cast<const char*>(
                    value.data()),
                static_cast<std::streamsize>(
                    value.size()));

            stream.flush();

            if (!stream) {
                std::filesystem::remove(
                    temporary,
                    error);

                return false;
            }
        }

#if defined(_WIN32)
        if (MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH) == 0) {

            std::filesystem::remove(
                temporary,
                error);

            return false;
        }
#else
        std::filesystem::rename(
            temporary,
            path,
            error);

        if (error) {
            std::filesystem::remove(
                temporary,
                error);

            return false;
        }
#endif

        return true;
    }
    catch (...) {
        return false;
    }
}

} // namespace

project_identity_store::project_identity_store(
    const std::filesystem::path& project_path)
    : identity_path(
          project_path.parent_path() /
          ".serverengine" /
          project_path.filename() /
          "project.identity") {
}

project_identity_store_result
project_identity_store::load(
    persisted_project_identity& output) const noexcept {

    output = {};

    image value;
    bool missing = false;

    if (!read_image(
            identity_path,
            value,
            missing)) {

        return
            project_identity_store_result::
                io_failed;
    }

    if (missing) {
        return
            project_identity_store_result::
                not_found;
    }

    if (!decode(
            value,
            output)) {

        output = {};

        return
            project_identity_store_result::
                invalid;
    }

    return
        project_identity_store_result::
            success;
}

project_identity_store_result
project_identity_store::save(
    const persisted_project_identity& identity) const noexcept {

    image value;

    if (!encode(
            identity,
            value)) {

        return
            project_identity_store_result::
                invalid;
    }

    return write_image(
        identity_path,
        value)
        ? project_identity_store_result::success
        : project_identity_store_result::io_failed;
}

}
