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
#include <vector>

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

class lexical_generation;

// Stable word descriptor over either native construction storage or encoded
// database.bin storage. Native descriptors resolve the arena's current backing
// allocation on every indexed read, so arena growth cannot invalidate suspended
// frontend frames.
class lexical_word_view final {
public:
    class iterator final {
    public:
        [[nodiscard]] std::uint32_t operator*() const noexcept {
            return (*owner)[index];
        }

        iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        friend bool operator==(const iterator& left, const iterator& right) noexcept {
            return left.owner == right.owner && left.index == right.index;
        }

        friend bool operator!=(const iterator& left, const iterator& right) noexcept {
            return !(left == right);
        }

    private:
        iterator(const lexical_word_view* value, std::size_t position) noexcept
            : owner(value), index(position) {}

        const lexical_word_view* owner = nullptr;
        std::size_t index = 0;
        friend class lexical_word_view;
    };

    lexical_word_view() noexcept = default;

    [[nodiscard]] static lexical_word_view from_encoded(
        std::span<const std::byte> values) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] std::uint32_t operator[](std::size_t index) const noexcept;
    [[nodiscard]] iterator begin() const noexcept { return iterator{this, 0}; }
    [[nodiscard]] iterator end() const noexcept { return iterator{this, count}; }

private:
    [[nodiscard]] static lexical_word_view from_native(
        const lexical_generation* owner,
        std::uint32_t arena,
        std::uint32_t offset,
        std::uint32_t count) noexcept;

    const lexical_generation* native_owner = nullptr;
    const std::byte* encoded = nullptr;
    std::uint32_t native_arena = invalid_lexical_arena;
    std::uint32_t native_offset = 0;
    std::uint32_t count = 0;

    friend class lexical_generation;
};

class lexical_directive_view final {
public:
    class iterator final {
    public:
        [[nodiscard]] lexical_directive_anchor operator*() const noexcept {
            return (*owner)[index];
        }

        iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        friend bool operator==(const iterator& left, const iterator& right) noexcept {
            return left.owner == right.owner && left.index == right.index;
        }

        friend bool operator!=(const iterator& left, const iterator& right) noexcept {
            return !(left == right);
        }

    private:
        iterator(const lexical_directive_view* value, std::size_t position) noexcept
            : owner(value), index(position) {}

        const lexical_directive_view* owner = nullptr;
        std::size_t index = 0;
        friend class lexical_directive_view;
    };

    lexical_directive_view() noexcept = default;

    [[nodiscard]] static lexical_directive_view from_native(
        std::span<const lexical_directive_anchor> values) noexcept;

    [[nodiscard]] static lexical_directive_view from_encoded(
        std::span<const std::byte> values) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] lexical_directive_anchor operator[](std::size_t index) const noexcept;
    [[nodiscard]] iterator begin() const noexcept { return iterator{this, 0}; }
    [[nodiscard]] iterator end() const noexcept { return iterator{this, count}; }

private:
    std::span<const lexical_directive_anchor> native;
    std::span<const std::byte> encoded;
    std::size_t count = 0;
};

struct lexical_file_view final {
    bool available = false;
    std::uint32_t token_count = 0;
    lexical_word_view words;
    lexical_directive_view directives;
};

class database_view;

// Borrowed immutable per-file lexical source. Persistence supplies the opaque
// reader; lexical_generation consumes this contract without depending on a
// database implementation or persistence translation unit.
class lexical_baseline_view final {
public:
    lexical_baseline_view() noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return context != nullptr && reader != nullptr && file_count_value != 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return file_count_value;
    }

    [[nodiscard]] bool file(file_id id, lexical_file_view& output) const noexcept {
        output = {};
        return valid() && id && id.value() <= file_count_value &&
            reader(context, id, output);
    }

private:
    using reader_function = bool (*)(
        const void* context,
        file_id file,
        lexical_file_view& output) noexcept;

    const void* context = nullptr;
    std::size_t file_count_value = 0;
    reader_function reader = nullptr;
    friend class database_view;
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

    // BUILD binds immutable database.bin state without allocating one mutable
    // record per committed file. The provider must outlive this generation.
    [[nodiscard]] server_status bind_baseline(
        const lexical_baseline_view& baseline,
        std::size_t arena_count) noexcept;

    // Single-owner boundary. Once marked, stale baseline tokens are hidden until
    // a replacement is published. Call before parallel producers begin.
    [[nodiscard]] server_status begin_replacement(
        file_id file) noexcept;

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

    [[nodiscard]] lexical_word_view words(
        file_id file) const noexcept;

    [[nodiscard]] lexical_directive_view directives(
        file_id file) const noexcept;

    [[nodiscard]] std::uint32_t token_count(
        file_id file) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return baseline_file_count + record_count;
    }

    [[nodiscard]] bool baseline_bound() const noexcept {
        return baseline.valid();
    }

private:
    friend class lexical_word_view;

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

    struct replacement_record final {
        file_id file{};
        lexical_record lexical;
        lexical_directive_record directives;
    };

    struct replacement_slot final {
        file_id file{};
        std::uint32_t record = 0;
    };

    static_assert(sizeof(replacement_slot) == 8);

    [[nodiscard]] bool local_index(file_id file, std::size_t& output) const noexcept;
    [[nodiscard]] bool find_replacement(file_id file, std::size_t& output) const noexcept;
    [[nodiscard]] server_status ensure_replacement_capacity(std::size_t additional) noexcept;
    void insert_replacement(
        std::vector<replacement_slot>& index,
        file_id file,
        std::uint32_t record) const noexcept;

    lexical_baseline_view baseline;
    std::size_t baseline_file_count = 0;

    std::unique_ptr<lexical_record[]> records;
    std::unique_ptr<lexical_directive_record[]> directive_records;
    std::size_t record_count = 0;
    std::size_t record_capacity = 0;

    std::unique_ptr<lexical_arena[]> arena_values;
    std::size_t arena_count_value = 0;

    std::vector<replacement_record> replacements;
    std::vector<replacement_slot> replacement_index;
};

}
