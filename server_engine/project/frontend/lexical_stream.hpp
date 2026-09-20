/*
 * Compact per-file lexical stream.
 *
 * lexical_stream owns one construction-time uint32 word stream for one
 * physical file. Common tokens occupy one word; rare extended source delta or
 * lexeme length values are encoded immediately after the token header.
 */
#pragma once

#include "lexical_token.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

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
        return values;
    }

    [[nodiscard]] std::size_t word_count() const noexcept {
        return values.size();
    }

    [[nodiscard]] std::uint32_t token_count() const noexcept {
        return tokens;
    }

    [[nodiscard]] bool empty() const noexcept {
        return tokens == 0;
    }

private:
    file_id source_file{};
    std::vector<std::uint32_t> values;
    std::uint32_t source_size = 0;
    std::uint32_t last_source_offset = 0;
    std::uint32_t tokens = 0;
};

}
