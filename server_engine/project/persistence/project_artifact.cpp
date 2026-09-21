#include "project_artifact.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> baseline_magic{
    std::byte{'C'}, std::byte{'W'}, std::byte{'B'}, std::byte{'A'},
    std::byte{'S'}, std::byte{'E'}, std::byte{'0'}, std::byte{'1'},
};

constexpr std::uint32_t baseline_format_version = 1;
constexpr std::uint32_t artifact_count =
    static_cast<std::uint32_t>(
        project_artifact_kind_count);

constexpr std::uint32_t change_token_flag =
    0x00000001u;

constexpr std::uint32_t known_proof_flags =
    change_token_flag;

constexpr std::size_t baseline_header_size = 24;
constexpr std::size_t proof_record_size = 72;
constexpr std::size_t baseline_checksum_size = 32;

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

    if (size == 0) {
        return;
    }

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

[[nodiscard]] file_content_hash checksum(
    std::span<const std::byte> bytes) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                bytes.data()),
            bytes.size()});
}

[[nodiscard]] bool valid_slot(
    project_artifact_slot slot) noexcept {

    return slot ==
            project_artifact_slot::slot0 ||
        slot ==
            project_artifact_slot::slot1;
}

}

project_artifact_layout_result make_project_artifact_layout(
    const std::filesystem::path& root_project_path,
    const server_files_configuration& files,
    project_artifact_layout& output) noexcept {

    output = {};

    try {
        if (root_project_path.empty() ||
            root_project_path.filename().empty()) {

            return project_artifact_layout_result::failed;
        }

        output.root =
            root_project_path.parent_path() /
            ".serverengine" /
            root_project_path.filename();

        output.baseline =
            output.root /
            files.baseline;

        constexpr std::array<const char*, 2> names{
            "slot0",
            "slot1",
        };

        for (std::size_t index = 0;
             index < output.slots.size();
             ++index) {

            auto& slot =
                output.slots[index];

            slot.directory =
                output.root /
                names[index];

            slot.manifest =
                slot.directory /
                files.manifest;

            slot.source_save =
                slot.directory /
                files.source_save;

            slot.database =
                slot.directory /
                files.database;

            slot.compiled =
                slot.directory /
                files.compiled;
        }

        return project_artifact_layout_result::success;
    }
    catch (...) {
        output = {};
        return project_artifact_layout_result::failed;
    }
}

void finalize_project_artifact_image(
    project_artifact_image& image) noexcept {

    image.hash =
        hash_file_content(
            std::string_view{
                reinterpret_cast<const char*>(
                    image.bytes.data()),
                image.bytes.size()});
}

project_baseline_image_result build_project_baseline_image(
    const project_baseline_descriptor& baseline,
    project_artifact_image& output) noexcept {

    output = {};

    if (!valid_slot(
            baseline.slot)) {

        return project_baseline_image_result::
            invalid_state;
    }

    try {
        output.bytes.reserve(
            baseline_header_size +
            project_artifact_kind_count *
                proof_record_size +
            baseline_checksum_size);

        append_bytes(
            output.bytes,
            baseline_magic.data(),
            baseline_magic.size());

        append_u32(
            output.bytes,
            baseline_format_version);

        append_u32(
            output.bytes,
            static_cast<std::uint32_t>(
                baseline.slot));

        append_u32(
            output.bytes,
            artifact_count);

        append_u32(
            output.bytes,
            0);

        for (const auto& proof :
             baseline.artifacts) {

            if (proof.size == 0 ||
                (proof.change_token_available &&
                 !proof.change_token)) {

                output = {};

                return project_baseline_image_result::
                    invalid_state;
            }

            append_u64(
                output.bytes,
                proof.size);

            append_u32(
                output.bytes,
                proof.change_token_available
                    ? change_token_flag
                    : 0);

            append_u32(
                output.bytes,
                0);

            append_bytes(
                output.bytes,
                proof.hash.bytes.data(),
                proof.hash.bytes.size());

            append_u64(
                output.bytes,
                proof.change_token_available
                    ? proof.change_token.volume_serial
                    : 0);

            append_u64(
                output.bytes,
                proof.change_token_available
                    ? proof.change_token.file_reference
                    : 0);

            append_u64(
                output.bytes,
                proof.change_token_available
                    ? static_cast<std::uint64_t>(
                        proof.change_token.file_usn)
                    : 0);
        }

        const auto digest =
            checksum(
                std::span<const std::byte>{
                    output.bytes});

        append_bytes(
            output.bytes,
            digest.bytes.data(),
            digest.bytes.size());

        finalize_project_artifact_image(
            output);

        return project_baseline_image_result::
            success;
    }
    catch (...) {
        output = {};
        return project_baseline_image_result::
            failed;
    }
}

project_baseline_image_result decode_project_baseline_image(
    std::span<const std::byte> image,
    project_baseline_descriptor& output) noexcept {

    output = {};

    constexpr auto expected_size =
        baseline_header_size +
        project_artifact_kind_count *
            proof_record_size +
        baseline_checksum_size;

    if (image.size() !=
            expected_size ||
        !std::equal(
            baseline_magic.begin(),
            baseline_magic.end(),
            image.begin())) {

        return project_baseline_image_result::
            invalid_image;
    }

    const auto payload_size =
        image.size() -
        baseline_checksum_size;

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

        return project_baseline_image_result::
            invalid_image;
    }

    std::size_t offset =
        baseline_magic.size();

    std::uint32_t version = 0;
    std::uint32_t slot = 0;
    std::uint32_t count = 0;
    std::uint32_t reserved = 0;

    if (!read_u32(
            image,
            offset,
            version) ||
        version !=
            baseline_format_version ||
        !read_u32(
            image,
            offset,
            slot) ||
        slot >
            static_cast<std::uint32_t>(
                project_artifact_slot::
                    slot1) ||
        !read_u32(
            image,
            offset,
            count) ||
        count != artifact_count ||
        !read_u32(
            image,
            offset,
            reserved) ||
        reserved != 0 ||
        offset !=
            baseline_header_size) {

        return project_baseline_image_result::
            invalid_image;
    }

    output.slot =
        static_cast<project_artifact_slot>(
            slot);

    for (auto& proof :
         output.artifacts) {

        std::uint32_t flags = 0;
        std::uint32_t proof_reserved = 0;
        std::uint64_t volume = 0;
        std::uint64_t reference = 0;
        std::uint64_t usn = 0;

        if (!read_u64(
                image,
                offset,
                proof.size) ||
            proof.size == 0 ||
            !read_u32(
                image,
                offset,
                flags) ||
            (flags &
                ~known_proof_flags) != 0 ||
            !read_u32(
                image,
                offset,
                proof_reserved) ||
            proof_reserved != 0) {

            output = {};

            return project_baseline_image_result::
                invalid_image;
        }

        if (offset > payload_size ||
            payload_size - offset <
                proof.hash.bytes.size()) {

            output = {};

            return project_baseline_image_result::
                invalid_image;
        }

        std::copy_n(
            image.begin() +
                static_cast<std::ptrdiff_t>(
                    offset),
            proof.hash.bytes.size(),
            proof.hash.bytes.begin());

        offset +=
            proof.hash.bytes.size();

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

            return project_baseline_image_result::
                invalid_image;
        }

        proof.change_token_available =
            (flags &
                change_token_flag) != 0;

        if (proof.change_token_available) {
            proof.change_token = {
                volume,
                reference,
                static_cast<std::int64_t>(
                    usn),
            };

            if (!proof.change_token) {
                output = {};

                return project_baseline_image_result::
                    invalid_image;
            }
        } else if (
            volume != 0 ||
            reference != 0 ||
            usn != 0) {

            output = {};

            return project_baseline_image_result::
                invalid_image;
        }
    }

    return offset == payload_size
        ? project_baseline_image_result::
            success
        : project_baseline_image_result::
            invalid_image;
}

project_artifact_open_result capture_project_artifact_proof(
    const std::filesystem::path& path,
    project_artifact_proof& output) noexcept {

    output = {};

    file_content_proof proof;

    const auto acquired =
        acquire_file_content_proof(
            path,
            proof);

    if (acquired ==
        file_content_result::missing) {

        return project_artifact_open_result::
            not_found;
    }

    if (acquired !=
        file_content_result::acquired) {

        return project_artifact_open_result::
            io_failed;
    }

    if (proof.observation.size == 0 ||
        proof.observation.size >
            static_cast<std::uintmax_t>(
                (std::numeric_limits<
                    std::uint64_t>::max)())) {

        return project_artifact_open_result::
            invalid;
    }

    output.size =
        static_cast<std::uint64_t>(
            proof.observation.size);

    output.hash =
        proof.content_hash;

    output.change_token =
        proof.change_token;

    output.change_token_available =
        proof.change_token_available;

    return project_artifact_open_result::
        success;
}

project_artifact_open_result open_verified_project_artifact(
    const std::filesystem::path& path,
    const project_artifact_proof& proof,
    read_only_file_mapping& output) noexcept {

    output.reset();

    const auto opened =
        output.open(
            path);

    if (opened ==
        read_only_file_mapping_result::
            not_found) {

        return project_artifact_open_result::
            not_found;
    }

    if (opened !=
            read_only_file_mapping_result::
                success ||
        proof.size == 0 ||
        output.size() !=
            proof.size) {

        output.reset();

        return opened ==
                read_only_file_mapping_result::
                    failed
            ? project_artifact_open_result::
                io_failed
            : project_artifact_open_result::
                invalid;
    }

    if (proof.change_token_available) {
        bool unchanged = false;

        const auto token =
            prove_file_unchanged(
                path,
                proof.change_token,
                unchanged);

        if (token ==
                file_token_result::
                    available &&
            unchanged) {

            return project_artifact_open_result::
                success;
        }

        if (token ==
            file_token_result::
                failed) {

            output.reset();

            return project_artifact_open_result::
                io_failed;
        }

        if (token ==
            file_token_result::
                missing) {

            output.reset();

            return project_artifact_open_result::
                not_found;
        }
    }

    const auto mapped =
        output.bytes();

    const auto actual =
        hash_file_content(
            std::string_view{
                reinterpret_cast<
                    const char*>(
                        mapped.data()),
                mapped.size()});

    if (!(actual ==
        proof.hash)) {

        output.reset();

        return project_artifact_open_result::
            invalid;
    }

    return project_artifact_open_result::
        success;
}

}
