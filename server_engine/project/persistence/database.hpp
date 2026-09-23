/*
 * Persisted BUILD frontend database.
 *
 * database.bin stores the exact Header/Source byte snapshots paired with their
 * retained per-file lexical state. It is not a Semantic DB. string_id and
 * identity_ref lineage remain canonically owned by compiled.bin and are not
 * duplicated here.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../frontend/lexical_generation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace cw::server {

enum class database_image_result : std::uint8_t {
    success,
    invalid_state,
    invalid_image,
    failed,
};

// Exact direct-encoding plan for one unchanged frontend construction state.
// The layout owns scalar offsets/counts only; File Context source bytes and
// lexical arenas remain authoritative.
class database_layout final {
public:
    database_layout() = default;

    [[nodiscard]] std::size_t size() const noexcept {
        return size_value;
    }

private:
    void reset() noexcept {
        *this = {};
    }

    std::size_t size_value = 0;
    std::size_t file_records_offset = 0;
    std::size_t file_records_size = 0;
    std::size_t source_bytes_offset = 0;
    std::size_t source_bytes_size = 0;
    std::size_t lexical_words_offset = 0;
    std::size_t lexical_words_size = 0;
    std::size_t lexical_directives_offset = 0;
    std::size_t lexical_directives_size = 0;
    std::size_t checksum_offset = 0;

    std::uint32_t file_count = 0;
    std::uint32_t word_count = 0;
    std::uint32_t directive_count = 0;

    friend database_image_result prepare_database_layout(
        const file_context&,
        const lexical_generation&,
        database_layout&) noexcept;

    friend database_image_result encode_database_image(
        const file_context&,
        const lexical_generation&,
        const database_layout&,
        std::span<std::byte>) noexcept;
};

using database_lexical_file_view = lexical_file_view;

// O(1) read-only mmap view used by BUILD. bind() validates fixed metadata and
// section geometry only; checksum/content scans stay on cold verification paths.
class database_view final {
public:
    database_view() noexcept = default;

    [[nodiscard]] database_image_result bind(
        std::span<const std::byte> image) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return !bytes.empty();
    }

    [[nodiscard]] std::size_t file_count() const noexcept {
        return file_count_value;
    }

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    [[nodiscard]] bool file(
        file_id id,
        database_lexical_file_view& output) const noexcept;

    [[nodiscard]] bool content(
        file_id id,
        std::string_view& output) const noexcept;

    [[nodiscard]] file_content_baseline_view
    content_baseline() const noexcept;

    [[nodiscard]] lexical_baseline_view lexical_baseline() const noexcept;

private:
    [[nodiscard]] static bool read_content_baseline(
        const void* context,
        file_id file,
        std::string_view& output) noexcept;

    [[nodiscard]] static bool read_lexical_baseline(
        const void* context,
        file_id file,
        lexical_file_view& output) noexcept;

    friend database_image_result validate_database_image(
        std::span<const std::byte>) noexcept;

    std::span<const std::byte> bytes;
    std::size_t file_records_offset = 0;
    std::size_t source_bytes_offset = 0;
    std::size_t source_bytes_size_value = 0;
    std::size_t lexical_words_offset = 0;
    std::size_t lexical_directives_offset = 0;

    std::uint32_t file_count_value = 0;
    std::uint32_t word_count_value = 0;
    std::uint32_t directive_count_value = 0;
};

[[nodiscard]] database_image_result prepare_database_layout(
    const file_context& files,
    const lexical_generation& lexical,
    database_layout& output) noexcept;

[[nodiscard]] database_image_result encode_database_image(
    const file_context& files,
    const lexical_generation& lexical,
    const database_layout& layout,
    std::span<std::byte> output) noexcept;

[[nodiscard]] database_image_result validate_database_image(
    std::span<const std::byte> image) noexcept;

[[nodiscard]] database_image_result verify_database_image(
    std::span<const std::byte> image,
    const file_context& files,
    const lexical_generation& lexical) noexcept;

}
