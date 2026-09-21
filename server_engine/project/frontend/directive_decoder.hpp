/*
 * Typed preprocessing-directive view over construction lexical input.
 *
 * directive_decoder consumes exactly one lexer-produced pp_* ... pp_end
 * sequence. It retains only file-local lexical/source ranges; directive
 * execution, include resolution, macro state, and dependency publication stay
 * outside this layer.
 */
#pragma once

#include "frontend_input.hpp"
#include "../../server_status.hpp"

#include <cstdint>

namespace cw::server {

struct source_range final {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

enum class directive_kind : std::uint8_t {
    invalid = 0,
    include,
    define,
    undef,
    if_,
    ifdef,
    ifndef,
    elif,
    else_,
    endif,
    line,
    error,
    pragma,
    unknown,
};

enum class include_form : std::uint8_t {
    none = 0,
    quoted,
    angled,
};

struct directive_range final {
    file_id file{};
    std::uint32_t word_offset = 0;
    std::uint32_t word_count = 0;
    source_range source;
};

struct include_directive final {
    include_form form = include_form::none;
    source_range locator;
};

struct identifier_directive final {
    source_range name;
    source_range replacement;
};

struct preprocessing_directive final {
    directive_kind kind = directive_kind::invalid;
    directive_range range;
    include_directive include;
    identifier_directive identifier;
};

// Stateless decoder for one preprocessing directive at the current input
// position. A successful call consumes through pp_end and leaves the input at
// the next lexical token.
class directive_decoder final {
public:
    [[nodiscard]] static server_status decode(
        frontend_input& input,
        preprocessing_directive& output) noexcept;
};

}
