#include "project_configuration_manifest_store.hpp"

#include "../filesystem_path.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
    std::byte{'M'},
    std::byte{'A'},
    std::byte{'N'},
    std::byte{'0'},
    std::byte{'2'},
};

constexpr std::uint32_t format_version = 2;
constexpr std::uint32_t change_token_flag = 0x01U;
constexpr std::uint32_t known_flags =
    change_token_flag;

constexpr std::size_t header_size = 48;
constexpr std::size_t fixed_entry_size = 72;
constexpr std::size_t checksum_size = 32;

void append_u32(
    std::vector<std::byte>& output,
    std::uint32_t value) {

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_u64(
    std::vector<std::byte>& output,
    std::uint64_t value) {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_bytes(
    std::vector<std::byte>& output,
    const std::byte* data,
    std::size_t size) {

    output.insert(
        output.end(),
        data,
        data + size);
}

[[nodiscard]] bool read_u32(
    const std::vector<std::byte>& input,
    std::size_t& offset,
    std::uint32_t& value) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 4) {

        return false;
    }

    value = 0;

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 4;
    return true;
}

[[nodiscard]] bool read_u64(
    const std::vector<std::byte>& input,
    std::size_t& offset,
    std::uint64_t& value) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 8) {

        return false;
    }

    value = 0;

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 8;
    return true;
}

[[nodiscard]] file_content_hash checksum(
    const std::vector<std::byte>& image,
    std::size_t size) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                image.data()),
            size});
}

[[nodiscard]] bool encode(
    const project_configuration_manifest& value,
    std::vector<std::byte>& output) {

    if (value.files.empty() ||
        value.files.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return false;
    }

    if (!(calculate_project_configuration_hash(
              value.files) ==
          value.configuration_hash)) {

        return false;
    }

    output.clear();

    append_bytes(
        output,
        magic.data(),
        magic.size());

    append_u32(
        output,
        format_version);

    append_u32(
        output,
        static_cast<std::uint32_t>(
            value.files.size()));

    append_bytes(
        output,
        value.configuration_hash.bytes.data(),
        value.configuration_hash.bytes.size());

    for (std::uint32_t index = 0;
         index < value.files.size();
         ++index) {

        const auto& file =
            value.files[index];

        std::string path;

        if (filesystem_path_to_utf8(file.path, path) !=
            filesystem_path_result::success) {

            return false;
        }

        if (path.empty() ||
            path.size() >
                (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        if (index == 0) {
            if (file.declaring_file !=
                    invalid_configuration_file ||
                file.path_type !=
                    project_configuration_path_type::relative ||
                file.path.is_absolute() ||
                file.path.has_root_name() ||
                file.path.has_root_directory() ||
                file.path !=
                    file.path.filename()) {

                return false;
            }
        } else {
            if (file.declaring_file >= index) {
                return false;
            }

            if (file.path_type ==
                    project_configuration_path_type::relative) {

                if (file.path.is_absolute() ||
                    file.path.has_root_name() ||
                    file.path.has_root_directory()) {

                    return false;
                }
            } else if (
                file.path_type ==
                    project_configuration_path_type::absolute) {

                if (!file.path.is_absolute()) {
                    return false;
                }
            } else {
                return false;
            }
        }

        append_u32(
            output,
            file.declaring_file);

        append_u32(
            output,
            static_cast<std::uint32_t>(
                file.path_type));

        append_u32(
            output,
            static_cast<std::uint32_t>(
                path.size()));

        std::uint32_t flags = 0;

        if (file.change_token_available) {
            if (!file.change_token) {
                return false;
            }

            flags |= change_token_flag;
        }

        append_u32(
            output,
            flags);

        append_bytes(
            output,
            file.content_hash.bytes.data(),
            file.content_hash.bytes.size());

        append_u64(
            output,
            file.change_token_available
                ? file.change_token.volume_serial
                : 0);

        append_u64(
            output,
            file.change_token_available
                ? file.change_token.file_reference
                : 0);

        append_u64(
            output,
            file.change_token_available
                ? static_cast<std::uint64_t>(
                    file.change_token.file_usn)
                : 0);

        append_bytes(
            output,
            reinterpret_cast<const std::byte*>(
                path.data()),
            path.size());
    }

    const auto digest =
        checksum(
            output,
            output.size());

    append_bytes(
        output,
        digest.bytes.data(),
        digest.bytes.size());

    return true;
}

[[nodiscard]] bool decode(
    const std::vector<std::byte>& input,
    project_configuration_manifest& output) {

    output = {};

    if (input.size() <
        header_size +
        fixed_entry_size +
        checksum_size) {

        return false;
    }

    if (!std::equal(
            magic.begin(),
            magic.end(),
            input.begin())) {

        return false;
    }

    const auto payload_size =
        input.size() - checksum_size;

    const auto expected =
        checksum(
            input,
            payload_size);

    if (!std::equal(
            expected.bytes.begin(),
            expected.bytes.end(),
            input.begin() +
                static_cast<std::ptrdiff_t>(
                    payload_size))) {

        return false;
    }

    std::size_t offset = 8;

    std::uint32_t version = 0;
    std::uint32_t count = 0;

    if (!read_u32(
            input,
            offset,
            version) ||
        version != format_version ||
        !read_u32(
            input,
            offset,
            count) ||
        count == 0) {

        return false;
    }

    if (offset > payload_size ||
        payload_size - offset <
            output.configuration_hash.bytes.size()) {

        return false;
    }

    std::copy_n(
        input.begin() +
            static_cast<std::ptrdiff_t>(
                offset),
        output.configuration_hash.bytes.size(),
        output.configuration_hash.bytes.begin());

    offset +=
        output.configuration_hash.bytes.size();

    try {
        output.files.reserve(count);

        for (std::uint32_t index = 0;
             index < count;
             ++index) {

            std::uint32_t declaring_file = 0;
            std::uint32_t path_type_raw = 0;
            std::uint32_t path_size = 0;
            std::uint32_t flags = 0;

            if (!read_u32(
                    input,
                    offset,
                    declaring_file) ||
                !read_u32(
                    input,
                    offset,
                    path_type_raw) ||
                path_type_raw >
                    static_cast<std::uint32_t>(
                        project_configuration_path_type::absolute) ||
                !read_u32(
                    input,
                    offset,
                    path_size) ||
                path_size == 0 ||
                !read_u32(
                    input,
                    offset,
                    flags) ||
                (flags & ~known_flags) != 0) {

                return false;
            }

            if (index == 0) {
                if (declaring_file !=
                        invalid_configuration_file ||
                    path_type_raw !=
                        static_cast<std::uint32_t>(
                            project_configuration_path_type::relative)) {

                    return false;
                }
            } else if (declaring_file >= index) {
                return false;
            }

            if (offset > payload_size ||
                payload_size - offset < 56 ||
                payload_size - offset - 56 <
                    path_size) {

                return false;
            }

            project_configuration_file_proof file;
            file.declaring_file =
                declaring_file;
            file.path_type =
                static_cast<project_configuration_path_type>(
                    path_type_raw);

            std::copy_n(
                input.begin() +
                    static_cast<std::ptrdiff_t>(
                        offset),
                file.content_hash.bytes.size(),
                file.content_hash.bytes.begin());

            offset +=
                file.content_hash.bytes.size();

            std::uint64_t volume = 0;
            std::uint64_t reference = 0;
            std::uint64_t usn = 0;

            if (!read_u64(
                    input,
                    offset,
                    volume) ||
                !read_u64(
                    input,
                    offset,
                    reference) ||
                !read_u64(
                    input,
                    offset,
                    usn)) {

                return false;
            }

            file.change_token_available =
                (flags & change_token_flag) != 0;

            if (file.change_token_available) {
                file.change_token = {
                    volume,
                    reference,
                    static_cast<std::int64_t>(
                        usn),
                };

                if (!file.change_token) {
                    return false;
                }
            } else if (
                volume != 0 ||
                reference != 0 ||
                usn != 0) {

                return false;
            }

            const std::string path{
                reinterpret_cast<const char*>(
                    input.data() + offset),
                path_size};

            if (filesystem_path_from_utf8(path, file.path) !=
                filesystem_path_result::success) {

                return false;
            }

            offset += path_size;

            if (file.path.empty()) {
                return false;
            }

            if (index == 0) {
                if (file.path.is_absolute() ||
                    file.path.has_root_name() ||
                    file.path.has_root_directory() ||
                    file.path !=
                        file.path.filename()) {

                    return false;
                }
            } else if (
                file.path_type ==
                    project_configuration_path_type::relative) {

                if (file.path.is_absolute() ||
                    file.path.has_root_name() ||
                    file.path.has_root_directory()) {

                    return false;
                }
            } else if (!file.path.is_absolute()) {
                return false;
            }

            output.files.push_back(
                std::move(file));
        }
    }
    catch (...) {
        output = {};
        return false;
    }

    if (offset != payload_size) {
        output = {};
        return false;
    }

    if (!(calculate_project_configuration_hash(
              output.files) ==
          output.configuration_hash)) {

        output = {};
        return false;
    }

    return true;
}

[[nodiscard]]
project_configuration_manifest_store_result
read_image(
    const std::filesystem::path& path,
    std::vector<std::byte>& output) noexcept {

    output.clear();

    try {
        std::error_code error;

        if (!std::filesystem::exists(
                path,
                error)) {

            return error
                ? project_configuration_manifest_store_result::
                    io_failed
                : project_configuration_manifest_store_result::
                    not_found;
        }

        const auto size =
            std::filesystem::file_size(
                path,
                error);

        if (error ||
            size >
                static_cast<std::uintmax_t>(
                    (std::numeric_limits<std::size_t>::max)())) {

            return project_configuration_manifest_store_result::
                io_failed;
        }

        output.resize(
            static_cast<std::size_t>(
                size));

        std::ifstream stream(
            path,
            std::ios::binary);

        if (!stream) {
            return project_configuration_manifest_store_result::
                io_failed;
        }

        if (!output.empty()) {
            stream.read(
                reinterpret_cast<char*>(
                    output.data()),
                static_cast<std::streamsize>(
                    output.size()));

            if (stream.gcount() !=
                static_cast<std::streamsize>(
                    output.size())) {

                return project_configuration_manifest_store_result::
                    io_failed;
            }
        }

        return project_configuration_manifest_store_result::
            success;
    }
    catch (...) {
        return project_configuration_manifest_store_result::
            io_failed;
    }
}

[[nodiscard]] bool write_image(
    const std::filesystem::path& path,
    const std::vector<std::byte>& value) noexcept {

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

}

project_configuration_manifest_store::
project_configuration_manifest_store(
    const std::filesystem::path& root_project_path)
    : manifest_path(
          root_project_path.parent_path() /
          ".serverengine" /
          root_project_path.filename() /
          "project.manifest") {
}

project_configuration_manifest_store_result
project_configuration_manifest_store::load(
    project_configuration_manifest& output) const noexcept {

    std::vector<std::byte> image;

    const auto read =
        read_image(
            manifest_path,
            image);

    if (read !=
        project_configuration_manifest_store_result::
            success) {

        output = {};
        return read;
    }

    if (!decode(
            image,
            output)) {

        output = {};

        return project_configuration_manifest_store_result::
            invalid;
    }

    return project_configuration_manifest_store_result::
        success;
}

project_configuration_manifest_store_result
project_configuration_manifest_store::save(
    const project_configuration_manifest& value) const noexcept {

    std::vector<std::byte> image;

    try {
        if (!encode(
                value,
                image)) {

            return project_configuration_manifest_store_result::
                invalid;
        }
    }
    catch (...) {
        return project_configuration_manifest_store_result::
            io_failed;
    }

    return write_image(
        manifest_path,
        image)
        ? project_configuration_manifest_store_result::
            success
        : project_configuration_manifest_store_result::
            io_failed;
}

}
