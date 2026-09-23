#include "source_save.hpp"
#include "source_save_format.hpp"

#include "../../filesystem_path.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace cw::server {
namespace {

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

struct path_index_slot final {
    std::uint32_t fingerprint = 0;
    file_id file{};
};

[[nodiscard]] bool decode_path_index_slot(
    std::span<const std::byte> bytes,
    std::size_t offset,
    path_index_slot& output) noexcept {

    output = {};

    std::uint32_t fingerprint = 0;
    std::uint32_t file = 0;

    if (!read_u32(
            bytes,
            offset,
            fingerprint) ||
        !read_u32(
            bytes,
            offset,
            file)) {

        return false;
    }

    output.fingerprint =
        fingerprint;

    output.file =
        file_id{file};

    return true;
}

}

bool source_save_view::contains(
    file_id file) const noexcept {

    return valid() &&
        file &&
        file.value() <=
            file_count_value;
}

bool source_save_view::file(
    file_id id,
    source_save_file_view& output) const noexcept {

    output = {};

    if (!contains(id)) {
        return false;
    }

    auto offset =
        records_offset +
        static_cast<std::size_t>(
            id.value() - 1) *
            source_save_record_size;

    std::uint32_t path_offset = 0;
    std::uint32_t path_size = 0;
    std::uint32_t kind = 0;
    std::uint32_t flags = 0;
    std::uint64_t file_reference = 0;
    std::uint32_t dependency_offset = 0;
    std::uint32_t dependency_count = 0;
    std::uint32_t dependent_offset = 0;
    std::uint32_t dependent_count = 0;

    if (!read_u32(
            bytes,
            offset,
            path_offset) ||
        !read_u32(
            bytes,
            offset,
            path_size) ||
        path_size == 0 ||
        !read_u32(
            bytes,
            offset,
            kind) ||
        kind >
            static_cast<std::uint32_t>(
                file_kind::assign) ||
        !read_u32(
            bytes,
            offset,
            flags) ||
        (flags &
            ~source_save_known_file_flags) != 0) {

        return false;
    }

    file_content_hash content_hash{};

    if (offset > bytes.size() ||
        bytes.size() - offset <
            content_hash.bytes.size()) {

        return false;
    }

    std::copy_n(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                offset),
        content_hash.bytes.size(),
        content_hash.bytes.begin());

    offset +=
        content_hash.bytes.size();

    if (!read_u64(
            bytes,
            offset,
            file_reference) ||
        !read_u32(
            bytes,
            offset,
            dependency_offset) ||
        !read_u32(
            bytes,
            offset,
            dependency_count) ||
        !read_u32(
            bytes,
            offset,
            dependent_offset) ||
        !read_u32(
            bytes,
            offset,
            dependent_count) ||
        path_offset >
            path_bytes_value ||
        path_size >
            path_bytes_value -
                path_offset ||
        dependency_offset >
            forward_count_value ||
        dependency_count >
            forward_count_value -
                dependency_offset ||
        dependent_offset >
            reverse_count_value ||
        dependent_count >
            reverse_count_value -
                dependent_offset) {

        return false;
    }

    const auto current =
        (flags &
            source_save_current_member_flag) != 0;

    const auto present =
        (flags &
            source_save_physical_present_flag) != 0;

    const auto has_reference =
        (flags &
            source_save_file_reference_flag) != 0;

    if (current &&
        !present) {

        return false;
    }

    if (has_reference !=
        (file_reference != 0)) {

        return false;
    }

    output.file = id;

    output.kind =
        static_cast<file_kind>(
            kind);

    output.current_member =
        current;

    output.physical.content_hash =
        content_hash;

    if (present) {
        output.physical.flags |=
            file_physical_present;
    }

    output.file_reference =
        file_reference;

    output.path_utf8 = {
        reinterpret_cast<const char*>(
            bytes.data() +
            paths_offset +
            path_offset),
        path_size,
    };

    output.dependencies =
        file_dependency_view::from_encoded(
            bytes.subspan(
                forward_offset +
                    static_cast<std::size_t>(
                        dependency_offset) *
                        sizeof(std::uint32_t),
                static_cast<std::size_t>(
                    dependency_count) *
                        sizeof(std::uint32_t)));

    output.dependents =
        file_dependency_view::from_encoded(
            bytes.subspan(
                reverse_offset +
                    static_cast<std::size_t>(
                        dependent_offset) *
                        sizeof(std::uint32_t),
                static_cast<std::size_t>(
                    dependent_count) *
                        sizeof(std::uint32_t)));

    return true;
}

server_status source_save_view::find_path(
    const std::filesystem::path& value,
    file_id& output) const noexcept {

    output = {};

    if (!valid() ||
        path_index_count_value == 0) {

        return server_status::
            project_artifact_invalid;
    }

    std::filesystem::path resolved;

    if (resolve_project_path(
            value,
            resolved) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    filesystem_path_key key;

    if (make_filesystem_path_key(
            resolved,
            key) !=
        filesystem_path_result::success) {

        return server_status::io_error;
    }

    const auto fingerprint =
        source_save_path_fingerprint(
            key);

    const auto mask =
        static_cast<std::size_t>(
            path_index_count_value - 1);

    auto position =
        static_cast<std::size_t>(
            fingerprint) &
        mask;

    for (std::size_t probe = 0;
         probe < path_index_count_value;
         ++probe) {

        path_index_slot slot;

        if (!decode_path_index_slot(
                bytes,
                path_index_offset +
                    position *
                        source_save_path_index_record_size,
                slot)) {

            return server_status::
                project_artifact_invalid;
        }

        if (slot.fingerprint == 0) {
            return slot.file
                ? server_status::
                    project_artifact_invalid
                : server_status::success;
        }

        if (!slot.file ||
            !contains(slot.file)) {

            return server_status::
                project_artifact_invalid;
        }

        if (slot.fingerprint ==
            fingerprint) {

            source_save_file_view state;

            if (!file(
                    slot.file,
                    state)) {

                return server_status::
                    project_artifact_invalid;
            }

            std::filesystem::path candidate;

            if (filesystem_path_from_utf8(
                    state.path_utf8,
                    candidate) !=
                filesystem_path_result::success) {

                return server_status::
                    project_artifact_invalid;
            }

            filesystem_path_key candidate_key;

            if (make_filesystem_path_key(
                    candidate,
                    candidate_key) !=
                filesystem_path_result::success) {

                return server_status::
                    project_artifact_invalid;
            }

            if (candidate_key == key) {
                output = slot.file;
                return server_status::success;
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return server_status::
        project_artifact_invalid;
}

}
