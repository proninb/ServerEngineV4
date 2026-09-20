/*
 * Construction lexical storage.
 *
 * lexical_generation owns direct-indexed per-file lexical ranges and one shared
 * uint32 word arena. file_id remains the only physical-file identity; workers
 * build private lexical_stream values and a single owner publishes them here.
 */
#pragma once

#include "lexer.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

inline constexpr std::uint32_t invalid_lexical_offset =
    0xffffffffu;

struct lexical_record final {
    std::uint32_t offset = invalid_lexical_offset;
    std::uint32_t word_count = 0;
    std::uint32_t token_count = 0;

    [[nodiscard]] constexpr bool available() const noexcept {
        return offset != invalid_lexical_offset;
    }
};

static_assert(sizeof(lexical_record) == 12);

struct lexical_failure final {
    file_id file{};
    lexical_error error;
};

class lexical_generation final {
public:
    lexical_generation() = default;

    lexical_generation(const lexical_generation&) = delete;
    lexical_generation& operator=(const lexical_generation&) = delete;

    [[nodiscard]] server_status reset(
        std::size_t file_count) noexcept;

    [[nodiscard]] server_status publish(
        file_id file,
        const lexical_stream& stream) noexcept;

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    [[nodiscard]] std::span<const std::uint32_t> words(
        file_id file) const noexcept;

    [[nodiscard]] std::uint32_t token_count(
        file_id file) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return records.size();
    }

private:
    std::vector<lexical_record> records;
    std::vector<std::uint32_t> word_arena;
};

[[nodiscard]] server_status build_lexical_generation(
    file_context& files,
    lexical_generation& output,
    lexical_failure* failure = nullptr) noexcept;

}
