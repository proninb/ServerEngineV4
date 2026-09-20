/*
 * Compact per-file lexical stream.
 *
 * lexical_stream owns only construction-time lexical tokens and sparse source
 * position recovery data. It has one physical file_id and no string_id,
 * identity_ref, preprocessing state, or semantic state.
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

inline constexpr std::uint32_t lexical_checkpoint_stride = 256;

struct lexical_checkpoint final {
    std::uint32_t token_index = 0;
    std::uint32_t source_offset = 0;
};

// Present only when a token cannot encode its source delta or source length
// inline. One record may carry either or both extended values.
struct lexical_extended_token final {
    std::uint32_t token_index = 0;
    std::uint32_t delta = 0;
    std::uint32_t length = 0;
};

static_assert(sizeof(lexical_checkpoint) == 8);
static_assert(sizeof(lexical_extended_token) == 12);

class lexical_stream final {
public:
    lexical_stream() = default;

    lexical_stream(const lexical_stream&) = delete;
    lexical_stream& operator=(const lexical_stream&) = delete;

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

    [[nodiscard]] std::span<const lexical_token> tokens() const noexcept {
        return values;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return values.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return values.empty();
    }

    [[nodiscard]] server_status source_offset(
        std::size_t token_index,
        std::uint32_t& output) const noexcept;

    [[nodiscard]] server_status source_length(
        std::size_t token_index,
        std::uint32_t& output) const noexcept;

private:
    [[nodiscard]] const lexical_extended_token* extended_at(
        std::size_t token_index) const noexcept;

    [[nodiscard]] std::uint32_t delta_at(
        std::size_t token_index) const noexcept;

    [[nodiscard]] std::uint32_t length_at(
        std::size_t token_index) const noexcept;

    file_id source_file{};
    std::vector<lexical_token> values;
    std::vector<lexical_checkpoint> checkpoints;
    std::vector<lexical_extended_token> extended_tokens;
    std::uint32_t source_size = 0;
    std::uint32_t last_source_offset = 0;
};

}
