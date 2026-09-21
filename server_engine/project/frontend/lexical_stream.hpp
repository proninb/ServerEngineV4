/*
 * Compact per-file lexical stream.
 *
 * One physical file is lexed once into compact words. The same pass records
 * sparse preprocessing-directive anchors so directive execution never rescans
 * ordinary C++ tokens.
 */
#pragma once

#include "lexical_token.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cw::server {

struct lexical_directive_anchor final {
    std::uint32_t word_offset = 0;
    std::uint32_t source_base = 0;
};

static_assert(sizeof(lexical_directive_anchor) == 8);

// Private reusable output of one physical-file lexer execution.
class lexical_stream final {
public:
    lexical_stream() = default;

    lexical_stream(const lexical_stream&) = delete;
    lexical_stream& operator=(const lexical_stream&) = delete;

    lexical_stream(lexical_stream&&) noexcept = default;
    lexical_stream& operator=(lexical_stream&&) noexcept = default;

    [[nodiscard]] server_status reset(
        file_id file,
        std::size_t source_size) noexcept;

    [[nodiscard]] server_status append(
        token_kind kind,
        std::uint32_t source_offset,
        std::uint32_t source_length) noexcept;

    [[nodiscard]] file_id file() const noexcept {
        return source_file;
    }

    [[nodiscard]] std::span<const std::uint32_t> words() const noexcept {
        return {
            values.get(),
            value_count,
        };
    }

    [[nodiscard]] std::span<const lexical_directive_anchor>
    directives() const noexcept {
        return {
            directive_values.get(),
            directive_count_value,
        };
    }

    [[nodiscard]] std::size_t word_count() const noexcept {
        return value_count;
    }

    [[nodiscard]] std::size_t directive_count() const noexcept {
        return directive_count_value;
    }

    [[nodiscard]] std::uint32_t token_count() const noexcept {
        return tokens;
    }

    [[nodiscard]] bool empty() const noexcept {
        return tokens == 0;
    }

private:
    [[nodiscard]] server_status reserve_words(
        std::size_t required) noexcept;

    [[nodiscard]] server_status reserve_directives(
        std::size_t required) noexcept;

    file_id source_file{};

    std::unique_ptr<std::uint32_t[]> values;
    std::size_t value_count = 0;
    std::size_t value_capacity = 0;

    std::unique_ptr<lexical_directive_anchor[]> directive_values;
    std::size_t directive_count_value = 0;
    std::size_t directive_capacity = 0;

    std::uint32_t source_size = 0;
    std::uint32_t last_source_offset = 0;
    std::uint32_t tokens = 0;
};

}
