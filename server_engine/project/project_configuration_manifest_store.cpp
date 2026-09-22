#include "project_configuration_manifest_store.hpp"

#include "../filesystem_path.hpp"
#include "../read_only_file_mapping.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

[[nodiscard]] bool add_size(
    std::size_t& value,
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            value) {

        return false;
    }

    value += additional;
    return true;
}

[[nodiscard]] bool write_u32(
    std::span<std::byte> output,
    std::size_t& offset,
    std::uint32_t value) noexcept {

    if (offset > output.size() ||
        output.size() - offset < 4) {

        return false;
    }

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU);
    }

    offset += 4;
    return true;
}

[[nodiscard]] bool write_u64(
    std::span<std::byte> output,
    std::size_t& offset,
    std::uint64_t value) noexcept {

    if (offset > output.size() ||
        output.size() - offset < 8) {

        return false;
    }

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU);
    }

    offset += 8;
    return true;
}

[[nodiscard]] bool write_bytes(
    std::span<std::byte> output,
    std::size_t& offset,
    const std::byte* data,
    std::size_t size) noexcept {

    if (offset > output.size() ||
        size > output.size() - offset ||
        (size != 0 && data == nullptr)) {

        return false;
    }

    if (size != 0) {
        std::copy_n(
            data,
            size,
            output.begin() +
                static_cast<std::ptrdiff_t>(
                    offset));
    }

    offset += size;
    return true;
}

[[nodiscard]] bool read_u32(
    std::span<const std::byte> input,
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
    std::span<const std::byte> input,
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
    std::span<const std::byte> image) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                image.data()),
            image.size()});
}

[[nodiscard]] bool validate_file(
    const project_configuration_file_proof& file,
    std::uint32_t index,
    std::string& path) noexcept {

    path.clear();

    if (filesystem_path_to_utf8(
            file.path,
            path) !=
            filesystem_path_result::success ||
        path.empty() ||
        path.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

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

    if (file.change_token_available &&
        !file.change_token) {

        return false;
    }

    return true;
}

}

project_configuration_manifest_store_result
prepare_project_configuration_manifest_layout(
    const project_configuration_manifest& value,
    project_configuration_manifest_layout& output) noexcept {

    output = {};

    if (value.files.empty() ||
        value.files.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return project_configuration_manifest_store_result::
            invalid;
    }

    try {
        if (!(calculate_project_configuration_hash(
                  value.files) ==
              value.configuration_hash)) {

            return project_configuration_manifest_store_result::
                invalid;
        }

        std::size_t size =
            header_size +
            checksum_size;

        std::string path;

        for (std::size_t position = 0;
             position < value.files.size();
             ++position) {

            const auto index =
                static_cast<std::uint32_t>(
                    position);

            if (!validate_file(
                    value.files[position],
                    index,
                    path) ||
                !add_size(
                    size,
                    fixed_entry_size) ||
                !add_size(
                    size,
                    path.size())) {

                return project_configuration_manifest_store_result::
                    invalid;
            }
        }

        output.size_value = size;
        output.file_count =
            static_cast<std::uint32_t>(
                value.files.size());
        output.configuration_hash =
            value.configuration_hash;

        return project_configuration_manifest_store_result::
            success;
    }
    catch (...) {
        output = {};

        return project_configuration_manifest_store_result::
            io_failed;
    }
}

project_configuration_manifest_store_result
encode_project_configuration_manifest(
    const project_configuration_manifest& value,
    const project_configuration_manifest_layout& layout,
    std::span<std::byte> output) noexcept {

    if (layout.size_value == 0 ||
        output.size() !=
            layout.size_value ||
        value.files.size() !=
            layout.file_count ||
        !(value.configuration_hash ==
          layout.configuration_hash)) {

        return project_configuration_manifest_store_result::
            invalid;
    }

    try {
        std::size_t offset = 0;

        if (!write_bytes(
                output,
                offset,
                magic.data(),
                magic.size()) ||
            !write_u32(
                output,
                offset,
                format_version) ||
            !write_u32(
                output,
                offset,
                layout.file_count) ||
            !write_bytes(
                output,
                offset,
                value.configuration_hash.bytes.data(),
                value.configuration_hash.bytes.size())) {

            return project_configuration_manifest_store_result::
                invalid;
        }

        std::string path;

        for (std::uint32_t index = 0;
             index < layout.file_count;
             ++index) {

            const auto& file =
                value.files[index];

            if (!validate_file(
                    file,
                    index,
                    path)) {

                return project_configuration_manifest_store_result::
                    invalid;
            }

            std::uint32_t flags = 0;

            if (file.change_token_available) {
                flags |= change_token_flag;
            }

            if (!write_u32(
                    output,
                    offset,
                    file.declaring_file) ||
                !write_u32(
                    output,
                    offset,
                    static_cast<std::uint32_t>(
                        file.path_type)) ||
                !write_u32(
                    output,
                    offset,
                    static_cast<std::uint32_t>(
                        path.size())) ||
                !write_u32(
                    output,
                    offset,
                    flags) ||
                !write_bytes(
                    output,
                    offset,
                    file.content_hash.bytes.data(),
                    file.content_hash.bytes.size()) ||
                !write_u64(
                    output,
                    offset,
                    file.change_token_available
                        ? file.change_token.volume_serial
                        : 0) ||
                !write_u64(
                    output,
                    offset,
                    file.change_token_available
                        ? file.change_token.file_reference
                        : 0) ||
                !write_u64(
                    output,
                    offset,
                    file.change_token_available
                        ? static_cast<std::uint64_t>(
                            file.change_token.file_usn)
                        : 0) ||
                !write_bytes(
                    output,
                    offset,
                    reinterpret_cast<const std::byte*>(
                        path.data()),
                    path.size())) {

                return project_configuration_manifest_store_result::
                    invalid;
            }
        }

        const auto payload_size =
            output.size() -
            checksum_size;

        if (offset != payload_size) {
            return project_configuration_manifest_store_result::
                invalid;
        }

        const auto digest =
            checksum(
                std::span<const std::byte>{
                    output.data(),
                    payload_size});

        if (!write_bytes(
                output,
                offset,
                digest.bytes.data(),
                digest.bytes.size()) ||
            offset != output.size()) {

            return project_configuration_manifest_store_result::
                invalid;
        }

        return project_configuration_manifest_store_result::
            success;
    }
    catch (...) {
        return project_configuration_manifest_store_result::
            io_failed;
    }
}

project_configuration_manifest_store_result
decode_project_configuration_manifest(
    std::span<const std::byte> image,
    project_configuration_manifest& output) noexcept {

    output = {};

    if (image.size() <
        header_size +
        fixed_entry_size +
        checksum_size) {

        return project_configuration_manifest_store_result::
            invalid;
    }

    if (!std::equal(
            magic.begin(),
            magic.end(),
            image.begin())) {

        return project_configuration_manifest_store_result::
            invalid;
    }

    const auto payload_size =
        image.size() -
        checksum_size;

    const auto expected =
        checksum(
            image.first(
                payload_size));

    if (!std::equal(
            expected.bytes.begin(),
            expected.bytes.end(),
            image.begin() +
                static_cast<std::ptrdiff_t>(
                    payload_size))) {

        return project_configuration_manifest_store_result::
            invalid;
    }

    try {
        std::size_t offset =
            magic.size();

        std::uint32_t version = 0;
        std::uint32_t count = 0;

        if (!read_u32(
                image,
                offset,
                version) ||
            version != format_version ||
            !read_u32(
                image,
                offset,
                count) ||
            count == 0 ||
            offset > payload_size ||
            payload_size - offset <
                output.configuration_hash.bytes.size()) {

            return project_configuration_manifest_store_result::
                invalid;
        }

        std::copy_n(
            image.begin() +
                static_cast<std::ptrdiff_t>(
                    offset),
            output.configuration_hash.bytes.size(),
            output.configuration_hash.bytes.begin());

        offset +=
            output.configuration_hash.bytes.size();

        output.files.reserve(count);

        for (std::uint32_t index = 0;
             index < count;
             ++index) {

            std::uint32_t declaring_file = 0;
            std::uint32_t path_type_raw = 0;
            std::uint32_t path_size = 0;
            std::uint32_t flags = 0;

            if (!read_u32(
                    image,
                    offset,
                    declaring_file) ||
                !read_u32(
                    image,
                    offset,
                    path_type_raw) ||
                path_type_raw >
                    static_cast<std::uint32_t>(
                        project_configuration_path_type::absolute) ||
                !read_u32(
                    image,
                    offset,
                    path_size) ||
                path_size == 0 ||
                !read_u32(
                    image,
                    offset,
                    flags) ||
                (flags & ~known_flags) != 0) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            if (index == 0) {
                if (declaring_file !=
                        invalid_configuration_file ||
                    path_type_raw !=
                        static_cast<std::uint32_t>(
                            project_configuration_path_type::relative)) {

                    output = {};
                    return project_configuration_manifest_store_result::
                        invalid;
                }
            } else if (
                declaring_file >= index) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            if (offset > payload_size ||
                payload_size - offset < 56 ||
                payload_size - offset - 56 <
                    path_size) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            project_configuration_file_proof file;
            file.declaring_file =
                declaring_file;
            file.path_type =
                static_cast<project_configuration_path_type>(
                    path_type_raw);

            std::copy_n(
                image.begin() +
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
                    image,
                    offset,
                    volume) ||
                !read_u64(
                    image,
                    offset,
                    reference) ||
                !read_u64(
                    image,
                    offset,
                    usn)) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
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
                    output = {};
                    return project_configuration_manifest_store_result::
                        invalid;
                }
            } else if (
                volume != 0 ||
                reference != 0 ||
                usn != 0) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            const std::string_view path{
                reinterpret_cast<const char*>(
                    image.data() +
                    offset),
                path_size};

            if (filesystem_path_from_utf8(
                    path,
                    file.path) !=
                    filesystem_path_result::success) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            offset +=
                path_size;

            if (file.path.empty()) {
                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            if (index == 0) {
                if (file.path.is_absolute() ||
                    file.path.has_root_name() ||
                    file.path.has_root_directory() ||
                    file.path !=
                        file.path.filename()) {

                    output = {};
                    return project_configuration_manifest_store_result::
                        invalid;
                }
            } else if (
                file.path_type ==
                    project_configuration_path_type::relative) {

                if (file.path.is_absolute() ||
                    file.path.has_root_name() ||
                    file.path.has_root_directory()) {

                    output = {};
                    return project_configuration_manifest_store_result::
                        invalid;
                }
            } else if (
                !file.path.is_absolute()) {

                output = {};
                return project_configuration_manifest_store_result::
                    invalid;
            }

            output.files.push_back(
                std::move(file));
        }

        if (offset != payload_size ||
            !(calculate_project_configuration_hash(
                  output.files) ==
              output.configuration_hash)) {

            output = {};
            return project_configuration_manifest_store_result::
                invalid;
        }

        return project_configuration_manifest_store_result::
            success;
    }
    catch (...) {
        output = {};

        return project_configuration_manifest_store_result::
            io_failed;
    }
}

project_configuration_manifest_store::
project_configuration_manifest_store(
    std::filesystem::path path)
    : manifest_path(
          std::move(path)) {
}

project_configuration_manifest_store_result
project_configuration_manifest_store::load(
    project_configuration_manifest& output) const noexcept {

    output = {};

    read_only_file_mapping mapping;

    const auto opened =
        mapping.open(
            manifest_path);

    if (opened ==
        read_only_file_mapping_result::
            not_found) {

        return project_configuration_manifest_store_result::
            not_found;
    }

    if (opened ==
        read_only_file_mapping_result::
            empty) {

        return project_configuration_manifest_store_result::
            invalid;
    }

    if (opened !=
        read_only_file_mapping_result::
            success) {

        return project_configuration_manifest_store_result::
            io_failed;
    }

    return decode_project_configuration_manifest(
        mapping.bytes(),
        output);
}

}
