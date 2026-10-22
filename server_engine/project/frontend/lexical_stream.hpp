/*
 * Compact per-file lexical stream.
 *
 * One physical file is lexed once into compact words. The same pass records
 * sparse preprocessing-directive anchors so directive execution never rescans
 * ordinary C++ tokens.
 */
#pragma once

#include "lexical_token.hpp"
#include "source_token.hpp"
#include <vector>
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
        std::uint32_t source_length, std::uint32_t lexical_hash = 0) noexcept;

    // Optional transient PUBLISH output; owner allocates one vector per file.
    void capture_source_tokens(std::vector<source_predecoded_token>* value, bool only = false) noexcept {
        source_tokens = value;
        header_tokens = nullptr;
        tokens_only = only;
        source_ready_only = only;
    }
    void capture_header_tokens(std::vector<header_lexical_token>* value, bool only = false) noexcept {
        header_tokens = value;
        source_tokens = nullptr;
        tokens_only = only;
        source_ready_only = false;
    }
    [[nodiscard]] bool source_tokens_only() const noexcept { return source_ready_only; }
    [[nodiscard]] bool header_tokens_only() const noexcept { return tokens_only && header_tokens != nullptr; }
    [[nodiscard]] server_status append_header_token(token_kind kind,
        std::uint32_t offset, std::uint32_t length) noexcept;
    [[nodiscard]] server_status append_source_token(token_kind kind, std::uint32_t offset,
        std::uint32_t length, std::uint32_t hash) noexcept;
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
    friend class semantic_input;
    // File-local words adopt an identity only when their exact prepared
    // physical snapshot is committed by active include replay.
    void bind_prepared_file(file_id file) noexcept { source_file = file; }
    [[nodiscard]] server_status reserve_words(
        std::size_t required) noexcept;

    [[nodiscard]] server_status reserve_directives(
        std::size_t required) noexcept;

    bool tokens_only = false;
    bool source_ready_only = false;
    std::vector<header_lexical_token>* header_tokens = nullptr;
    std::vector<source_predecoded_token>* source_tokens = nullptr;
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
