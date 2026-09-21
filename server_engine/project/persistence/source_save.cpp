#include "source_save.hpp"

#include "../../filesystem_path.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'C'}, std::byte{'W'}, std::byte{'S'}, std::byte{'R'},
    std::byte{'C'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'},
};

constexpr std::uint32_t format_version = 1;
constexpr std::uint32_t current_member_flag = 0x00000001u;
constexpr std::uint32_t physical_present_flag = 0x00000002u;
constexpr std::uint32_t change_token_flag = 0x00000004u;
constexpr std::uint32_t known_flags =
    current_member_flag |
    physical_present_flag |
    change_token_flag;

constexpr std::size_t header_size = 32;
constexpr std::size_t record_size = 88;
constexpr std::size_t checksum_size = 32;

struct decoded_record final {
    std::uint32_t path_offset = 0;
    std::uint32_t path_size = 0;
    file_kind kind = file_kind::project;
    std::uint32_t flags = 0;
    file_content_hash content_hash{};
    file_change_token change_token{};
    std::uint32_t dependency_offset = 0;
    std::uint32_t dependency_count = 0;
    std::uint32_t dependent_offset = 0;
    std::uint32_t dependent_count = 0;
};

void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) & 0xffU));
    }
}

void append_u64(std::vector<std::byte>& output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) & 0xffU));
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
    std::span<const std::byte> input,
    std::size_t& offset,
    std::uint32_t& output) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 4) {

        return false;
    }

    output = 0;

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output |=
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
    std::uint64_t& output) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 8) {

        return false;
    }

    output = 0;

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 8;
    return true;
}

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

[[nodiscard]] bool multiply_size(
    std::size_t left,
    std::size_t right,
    std::size_t& output) noexcept {

    if (left != 0 &&
        right >
            (std::numeric_limits<std::size_t>::max)() /
                left) {

        return false;
    }

    output = left * right;
    return true;
}

[[nodiscard]] file_content_hash checksum(
    std::span<const std::byte> bytes) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                bytes.data()),
            bytes.size()});
}

[[nodiscard]] bool valid_file_id(
    std::uint32_t value,
    std::uint32_t file_count) noexcept {

    return value != 0 &&
        value <= file_count;
}

}

source_save_result build_source_save_image(
    const file_context& files,
    project_artifact_image& output) noexcept {

    output = {};

    if (!files.dependency_topology_finalized() ||
        files.size() == 0 ||
        files.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return source_save_result::invalid_state;
    }

    try {
        const auto file_count =
            static_cast<std::uint32_t>(
                files.size());

        std::vector<std::string> paths;
        paths.reserve(files.size());

        std::uint32_t path_bytes = 0;
        std::uint32_t forward_count = 0;
        std::uint32_t reverse_count = 0;

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            const file_id file{value};
            const auto* physical =
                files.physical(file);

            if (physical == nullptr) {
                return source_save_result::invalid_state;
            }

            const auto path_view =
                files.path(file);

            const std::filesystem::path path{
                path_view.begin(),
                path_view.end()};

            std::string utf8_path;

            if (filesystem_path_to_utf8(
                    path,
                    utf8_path) !=
                    filesystem_path_result::success ||
                utf8_path.empty() ||
                utf8_path.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()) -
                        path_bytes) {

                return source_save_result::invalid_state;
            }

            path_bytes +=
                static_cast<std::uint32_t>(
                    utf8_path.size());

            paths.push_back(
                std::move(utf8_path));

            const auto dependencies =
                files.dependencies(file);

            const auto dependents =
                files.dependents(file);

            if (dependencies.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()) -
                        forward_count ||
                dependents.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()) -
                        reverse_count) {

                return source_save_result::invalid_state;
            }

            forward_count +=
                static_cast<std::uint32_t>(
                    dependencies.size());

            reverse_count +=
                static_cast<std::uint32_t>(
                    dependents.size());
        }

        if (forward_count != reverse_count) {
            return source_save_result::invalid_state;
        }

        std::size_t total_size = header_size;
        std::size_t records_size = 0;
        std::size_t forward_size = 0;
        std::size_t reverse_size = 0;

        if (!multiply_size(files.size(), record_size, records_size) ||
            !multiply_size(
                forward_count,
                sizeof(std::uint32_t),
                forward_size) ||
            !multiply_size(
                reverse_count,
                sizeof(std::uint32_t),
                reverse_size) ||
            !add_size(total_size, records_size) ||
            !add_size(total_size, path_bytes) ||
            !add_size(total_size, forward_size) ||
            !add_size(total_size, reverse_size) ||
            !add_size(total_size, checksum_size)) {

            return source_save_result::failed;
        }

        output.bytes.reserve(total_size);

        append_bytes(
            output.bytes,
            magic.data(),
            magic.size());

        append_u32(output.bytes, format_version);
        append_u32(output.bytes, file_count);
        append_u32(output.bytes, path_bytes);
        append_u32(output.bytes, forward_count);
        append_u32(output.bytes, reverse_count);
        append_u32(output.bytes, 0);

        std::uint32_t path_offset = 0;
        std::uint32_t dependency_offset = 0;
        std::uint32_t dependent_offset = 0;

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            const file_id file{value};
            const auto* physical =
                files.physical(file);

            if (physical == nullptr) {
                output = {};
                return source_save_result::invalid_state;
            }

            const auto dependencies =
                files.dependencies(file);

            const auto dependents =
                files.dependents(file);

            const auto& path =
                paths[value - 1];

            append_u32(output.bytes, path_offset);
            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    path.size()));

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    files.kind(file)));

            std::uint32_t flags =
                current_member_flag;

            if (physical->present()) {
                flags |= physical_present_flag;
            }

            if (physical->has_change_token()) {
                if (!physical->change_token) {
                    output = {};
                    return source_save_result::invalid_state;
                }

                flags |= change_token_flag;
            }

            append_u32(output.bytes, flags);

            append_bytes(
                output.bytes,
                physical->content_hash.bytes.data(),
                physical->content_hash.bytes.size());

            append_u64(
                output.bytes,
                physical->has_change_token()
                    ? physical->change_token.volume_serial
                    : 0);

            append_u64(
                output.bytes,
                physical->has_change_token()
                    ? physical->change_token.file_reference
                    : 0);

            append_u64(
                output.bytes,
                physical->has_change_token()
                    ? static_cast<std::uint64_t>(
                        physical->change_token.file_usn)
                    : 0);

            append_u32(
                output.bytes,
                dependency_offset);

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    dependencies.size()));

            append_u32(
                output.bytes,
                dependent_offset);

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    dependents.size()));

            path_offset +=
                static_cast<std::uint32_t>(
                    path.size());

            dependency_offset +=
                static_cast<std::uint32_t>(
                    dependencies.size());

            dependent_offset +=
                static_cast<std::uint32_t>(
                    dependents.size());
        }

        for (const auto& path : paths) {
            append_bytes(
                output.bytes,
                reinterpret_cast<const std::byte*>(
                    path.data()),
                path.size());
        }

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            for (const auto target :
                 files.dependencies(
                     file_id{value})) {

                append_u32(
                    output.bytes,
                    target.value());
            }
        }

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            for (const auto source :
                 files.dependents(
                     file_id{value})) {

                append_u32(
                    output.bytes,
                    source.value());
            }
        }

        const auto digest =
            checksum(
                std::span<const std::byte>{
                    output.bytes});

        append_bytes(
            output.bytes,
            digest.bytes.data(),
            digest.bytes.size());

        if (output.bytes.size() != total_size) {
            output = {};
            return source_save_result::failed;
        }

        finalize_project_artifact_image(output);

        return source_save_result::success;
    }
    catch (...) {
        output = {};
        return source_save_result::failed;
    }
}

source_save_result validate_source_save_image(
    std::span<const std::byte> image) noexcept {

    if (image.size() <
        header_size +
        record_size +
        checksum_size) {

        return source_save_result::invalid_image;
    }

    if (!std::equal(
            magic.begin(),
            magic.end(),
            image.begin())) {

        return source_save_result::invalid_image;
    }

    const auto payload_size =
        image.size() -
        checksum_size;

    const auto expected =
        checksum(
            image.first(payload_size));

    if (!std::equal(
            expected.bytes.begin(),
            expected.bytes.end(),
            image.begin() +
                static_cast<std::ptrdiff_t>(
                    payload_size))) {

        return source_save_result::invalid_image;
    }

    std::size_t offset = magic.size();

    std::uint32_t version = 0;
    std::uint32_t file_count = 0;
    std::uint32_t path_bytes = 0;
    std::uint32_t forward_count = 0;
    std::uint32_t reverse_count = 0;
    std::uint32_t reserved = 0;

    if (!read_u32(image, offset, version) ||
        version != format_version ||
        !read_u32(image, offset, file_count) ||
        file_count == 0 ||
        !read_u32(image, offset, path_bytes) ||
        !read_u32(image, offset, forward_count) ||
        !read_u32(image, offset, reverse_count) ||
        forward_count != reverse_count ||
        !read_u32(image, offset, reserved) ||
        reserved != 0 ||
        offset != header_size) {

        return source_save_result::invalid_image;
    }

    std::size_t records_size = 0;
    std::size_t forward_size = 0;
    std::size_t reverse_size = 0;

    if (!multiply_size(file_count, record_size, records_size) ||
        !multiply_size(
            forward_count,
            sizeof(std::uint32_t),
            forward_size) ||
        !multiply_size(
            reverse_count,
            sizeof(std::uint32_t),
            reverse_size)) {

        return source_save_result::invalid_image;
    }

    std::size_t expected_size =
        header_size;

    if (!add_size(expected_size, records_size) ||
        !add_size(expected_size, path_bytes) ||
        !add_size(expected_size, forward_size) ||
        !add_size(expected_size, reverse_size) ||
        !add_size(expected_size, checksum_size) ||
        expected_size != image.size()) {

        return source_save_result::invalid_image;
    }

    try {
        std::vector<decoded_record> records(
            file_count);

        for (std::uint32_t index = 0;
             index < file_count;
             ++index) {

            auto& record =
                records[index];

            std::uint32_t kind = 0;
            std::uint64_t volume = 0;
            std::uint64_t reference = 0;
            std::uint64_t usn = 0;

            if (!read_u32(
                    image,
                    offset,
                    record.path_offset) ||
                !read_u32(
                    image,
                    offset,
                    record.path_size) ||
                record.path_size == 0 ||
                !read_u32(
                    image,
                    offset,
                    kind) ||
                kind >
                    static_cast<std::uint32_t>(
                        file_kind::assign) ||
                !read_u32(
                    image,
                    offset,
                    record.flags) ||
                (record.flags & ~known_flags) != 0) {

                return source_save_result::invalid_image;
            }

            record.kind =
                static_cast<file_kind>(kind);

            if (offset > payload_size ||
                payload_size - offset <
                    record.content_hash.bytes.size()) {

                return source_save_result::invalid_image;
            }

            std::copy_n(
                image.begin() +
                    static_cast<std::ptrdiff_t>(
                        offset),
                record.content_hash.bytes.size(),
                record.content_hash.bytes.begin());

            offset +=
                record.content_hash.bytes.size();

            if (!read_u64(image, offset, volume) ||
                !read_u64(image, offset, reference) ||
                !read_u64(image, offset, usn) ||
                !read_u32(
                    image,
                    offset,
                    record.dependency_offset) ||
                !read_u32(
                    image,
                    offset,
                    record.dependency_count) ||
                !read_u32(
                    image,
                    offset,
                    record.dependent_offset) ||
                !read_u32(
                    image,
                    offset,
                    record.dependent_count)) {

                return source_save_result::invalid_image;
            }

            if ((record.flags & change_token_flag) != 0) {
                if ((record.flags &
                        physical_present_flag) == 0) {

                    return source_save_result::invalid_image;
                }

                record.change_token = {
                    volume,
                    reference,
                    static_cast<std::int64_t>(
                        usn),
                };

                if (!record.change_token) {
                    return source_save_result::invalid_image;
                }
            } else if (
                volume != 0 ||
                reference != 0 ||
                usn != 0) {

                return source_save_result::invalid_image;
            }

            if (record.path_offset > path_bytes ||
                record.path_size >
                    path_bytes -
                        record.path_offset ||
                record.dependency_offset >
                    forward_count ||
                record.dependency_count >
                    forward_count -
                        record.dependency_offset ||
                record.dependent_offset >
                    reverse_count ||
                record.dependent_count >
                    reverse_count -
                        record.dependent_offset) {

                return source_save_result::invalid_image;
            }
        }

        const auto path_section =
            header_size +
            records_size;

        const auto forward_section =
            path_section +
            path_bytes;

        const auto reverse_section =
            forward_section +
            forward_size;

        if (reverse_section +
                reverse_size !=
            payload_size) {

            return source_save_result::invalid_image;
        }

        for (const auto& record : records) {
            const auto begin =
                path_section +
                record.path_offset;

            const std::string_view utf8_path{
                reinterpret_cast<const char*>(
                    image.data() + begin),
                record.path_size};

            std::filesystem::path decoded_path;

            if (filesystem_path_from_utf8(
                    utf8_path,
                    decoded_path) !=
                    filesystem_path_result::success ||
                decoded_path.empty()) {

                return source_save_result::invalid_image;
            }
        }

        auto read_edge =
            [&](std::size_t section,
                std::uint32_t index,
                std::uint32_t& value) noexcept {

            std::size_t cursor =
                section +
                static_cast<std::size_t>(
                    index) *
                    sizeof(std::uint32_t);

            return read_u32(
                image,
                cursor,
                value);
        };

        std::vector<std::uint32_t>
            expected_reverse_counts(
                file_count);

        for (std::uint32_t source_index = 0;
             source_index < file_count;
             ++source_index) {

            const auto& record =
                records[source_index];

            for (std::uint32_t edge = 0;
                 edge < record.dependency_count;
                 ++edge) {

                std::uint32_t target = 0;

                if (!read_edge(
                        forward_section,
                        record.dependency_offset +
                            edge,
                        target) ||
                    !valid_file_id(
                        target,
                        file_count)) {

                    return source_save_result::invalid_image;
                }

                auto& count =
                    expected_reverse_counts[
                        target - 1];

                if (count ==
                    (std::numeric_limits<std::uint32_t>::max)()) {

                    return source_save_result::invalid_image;
                }

                ++count;
            }
        }

        std::uint32_t reverse_offset = 0;

        for (std::uint32_t index = 0;
             index < file_count;
             ++index) {

            if (records[index].dependent_offset !=
                    reverse_offset ||
                records[index].dependent_count !=
                    expected_reverse_counts[index] ||
                expected_reverse_counts[index] >
                    (std::numeric_limits<std::uint32_t>::max)() -
                        reverse_offset) {

                return source_save_result::invalid_image;
            }

            reverse_offset +=
                expected_reverse_counts[index];
        }

        if (reverse_offset != reverse_count) {
            return source_save_result::invalid_image;
        }

        std::vector<std::uint32_t> cursor(
            file_count);

        for (std::uint32_t index = 0;
             index < file_count;
             ++index) {

            cursor[index] =
                records[index]
                    .dependent_offset;
        }

        std::vector<std::uint32_t> expected_reverse(
            reverse_count);

        for (std::uint32_t source_index = 0;
             source_index < file_count;
             ++source_index) {

            const auto& record =
                records[source_index];

            const auto source =
                source_index + 1;

            for (std::uint32_t edge = 0;
                 edge < record.dependency_count;
                 ++edge) {

                std::uint32_t target = 0;

                if (!read_edge(
                        forward_section,
                        record.dependency_offset +
                            edge,
                        target)) {

                    return source_save_result::invalid_image;
                }

                expected_reverse[
                    cursor[target - 1]++] =
                    source;
            }
        }

        for (std::uint32_t index = 0;
             index < reverse_count;
             ++index) {

            std::uint32_t persisted = 0;

            if (!read_edge(
                    reverse_section,
                    index,
                    persisted) ||
                !valid_file_id(
                    persisted,
                    file_count) ||
                persisted !=
                    expected_reverse[index]) {

                return source_save_result::invalid_image;
            }
        }

        return source_save_result::success;
    }
    catch (...) {
        return source_save_result::failed;
    }
}

}

