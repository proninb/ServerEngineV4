/*
 * Streaming Frontend lexical-input stack.
 *
 * frontend_input owns only active per-file lexical positions. Lexical storage
 * remains owned by lexical_generation; each frame retains one stable lexical
 * descriptor, so token iteration performs no repeated file/baseline lookup and
 * arena growth during include discovery cannot invalidate suspended positions.
 */
#pragma once

#include "lexical_generation.hpp"
#include "lexical_token.hpp"
#include "../../server_status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cw::server {

inline constexpr std::size_t frontend_include_depth_limit = 256;

struct frontend_token final {
    file_id file{};
    token_kind kind = token_kind::invalid;
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
};

static_assert(sizeof(frontend_token) == 16);

// Shared checked decoder; Source borrows a native span, Header keeps a stable
// descriptor that survives arena growth during includes.
template<class Words, class Token>
[[nodiscard]] server_status decode_frontend_token(
    const Words& words, file_id file, std::uint32_t& word_offset,
    std::uint32_t& source_offset, Token& output) noexcept {
    output = {};
    auto position =
        static_cast<std::size_t>(
            word_offset);

    if (position >= words.size()) {
        return server_status::project_configuration_invalid;
    }

    const auto header =
        lexical_token::from_value(
            words[position++]);

    if (!header) {
        return server_status::project_configuration_invalid;
    }

    auto delta =
        header.delta();

    if (delta ==
        lexical_token::extended_delta) {

        if (position >= words.size()) {
            return server_status::project_configuration_invalid;
        }

        delta =
            words[position++];
    }

    auto length =
        header.length();

    if (length ==
        lexical_token::extended_length) {

        if (position >= words.size()) {
            return server_status::project_configuration_invalid;
        }

        length =
            words[position++];
    }

    const auto first =
        word_offset == 0;

    if (!first &&
        delta >
            (std::numeric_limits<std::uint32_t>::max)() -
                source_offset) {

        return server_status::project_configuration_invalid;
    }

    const auto source =
        first
            ? delta
            : source_offset + delta;

    if (position >
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::project_configuration_invalid;
    }

    word_offset =
        static_cast<std::uint32_t>(
            position);

    source_offset =
        source;

    output = {
        file,
        header.kind(),
        source,
        length,
    };

    return server_status::success;
}

// Active lexical-input stack for Parser/Semantic streaming and exact directive
// decoding. start_at() enters a sparse lexer-recorded directive anchor without
// scanning the ordinary tokens that precede it.
class frontend_input final {
public:
    explicit frontend_input(
        const lexical_generation& lexical) noexcept
        : lexical(lexical) {
    }

    frontend_input(const frontend_input&) = delete;
    frontend_input& operator=(const frontend_input&) = delete;

    [[nodiscard]] server_status start(
        file_id root) noexcept;

    [[nodiscard]] server_status start_at(
        file_id file,
        std::uint32_t word_offset,
        std::uint32_t source_base) noexcept;

    [[nodiscard]] server_status enter(
        file_id file) noexcept;

    [[nodiscard]] server_status next(
        frontend_token& output) noexcept;

    [[nodiscard]] server_status leave() noexcept;

    [[nodiscard]] file_id current_file() const noexcept;

    [[nodiscard]] std::uint32_t word_offset() const noexcept;

    [[nodiscard]] std::uint32_t source_offset() const noexcept;

    [[nodiscard]] bool finished() const noexcept;

    [[nodiscard]] bool empty() const noexcept {
        return stack_size == 0;
    }

    [[nodiscard]] std::size_t depth() const noexcept {
        return stack_size;
    }

private:
    struct frame final {
        file_id file{};
        std::uint32_t word_offset = 0;
        std::uint32_t source_offset = 0;
        lexical_word_view words;
    };

    static_assert(sizeof(frame) <= 48);

    const lexical_generation& lexical;
    std::array<frame, frontend_include_depth_limit> stack{};
    std::size_t stack_size = 0;
};

}
