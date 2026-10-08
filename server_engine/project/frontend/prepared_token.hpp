/*
 * Parser V2 prepared semantic token boundary.
 *
 * Identifier tokens already carry canonical string_id. Parser V2 therefore
 * has no physical-file, lexer, spelling-hash, or string-intern responsibility.
 */
#pragma once

#include "lexical_token.hpp"
#include "../file/file_id.hpp"
#include "../../string_id.hpp"

#include <cstdint>
#include <type_traits>

namespace cw::server {

enum class prepared_number_kind_v2 : std::uint8_t {
    none = 0,
    unsigned_integer,
};

struct prepared_number_v2 final {
    std::uint64_t bits = 0;
    prepared_number_kind_v2 kind =
        prepared_number_kind_v2::none;
    std::uint8_t reserved[7]{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return kind !=
            prepared_number_kind_v2::none;
    }
};

static_assert(sizeof(prepared_number_v2) == 16);
static_assert(std::is_trivially_copyable_v<prepared_number_v2>);

struct prepared_token final {
    file_id file{};
    token_kind kind = token_kind::invalid;
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
    string_id identifier{};
    prepared_number_v2 number{};
};

static_assert(std::is_trivially_copyable_v<prepared_token>);

}
