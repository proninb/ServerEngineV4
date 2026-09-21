/*
 * Construction lexical storage.
 *
 * Physical files publish into CPU-lane-owned arenas. Per-file records remain
 * direct-indexed by file_id, so parallel producers need no shared append point,
 * mutex, atomic work index, or per-file task object.
 */
#pragma once

#include "lexer.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cw::server {

inline constexpr std::uint32_t invalid_lexical_arena =
    0xffffffffu;

struct lexical_record final {
    std::uint32_t arena = invalid_lexical_arena;
    std::uint32_t word_offset = 0;
    std::uint32_t word_count = 0;
    std::uint32_t token_count = 0;

    [[nodiscard]] constexpr bool available() const noexcept {
        return arena != invalid_lexical_arena;
    }
};

static_assert(sizeof(lexical_record) == 16);

struct lexical_directive_record final {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

static_assert(sizeof(lexical_directive_record) == 8);

struct lexical_failure final {
    file_id file{};
    lexical_error error;
};

// Retained construction lexical facts. Each arena has one producer in every
// parallel frontier. Record-table growth is a single-owner operation performed
// before workers publish newly discovered file_id values.
class lexical_generation final {
public:
    lexical_generation() = default;

    lexical_generation(const lexical_generation&) = delete;
    lexical_generation& operator=(const lexical_generation&) = delete;

    [[nodiscard]] server_status reset(
        std::size_t file_count,
        std::size_t arena_count) noexcept;

    // Single-owner boundary. Extends direct-indexed records before a parallel
    // frontier publishes appended file_id values.
    [[nodiscard]] server_status extend(
        std::size_t file_count) noexcept;

    [[nodiscard]] server_status publish(
        file_id file,
        std::uint32_t arena,
        const lexical_stream& stream) noexcept;

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    [[nodiscard]] std::span<const std::uint32_t> words(
        file_id file) const noexcept;

    [[nodiscard]] std::span<const lexical_directive_anchor> directives(
        file_id file) const noexcept;

    [[nodiscard]] std::uint32_t token_count(
        file_id file) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return record_count;
    }

private:
    struct lexical_arena final {
        std::unique_ptr<std::uint32_t[]> words;
        std::size_t word_count = 0;
        std::size_t word_capacity = 0;

        std::unique_ptr<lexical_directive_anchor[]> directives;
        std::size_t directive_count = 0;
        std::size_t directive_capacity = 0;
    };

    [[nodiscard]] server_status ensure_records(
        std::size_t required) noexcept;

    [[nodiscard]] static server_status ensure_words(
        lexical_arena& arena,
        std::size_t required) noexcept;

    [[nodiscard]] static server_status ensure_directives(
        lexical_arena& arena,
        std::size_t required) noexcept;

    std::unique_ptr<lexical_record[]> records;
    std::unique_ptr<lexical_directive_record[]> directive_records;
    std::size_t record_count = 0;
    std::size_t record_capacity = 0;

    std::unique_ptr<lexical_arena[]> arena_values;
    std::size_t arena_count_value = 0;
};

}
