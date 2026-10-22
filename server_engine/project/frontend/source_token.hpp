#pragma once
#include "lexical_token.hpp"
#include "../../string_id.hpp"
#include <cstdint>

namespace cw::server {
// Header already keeps identifier hashes in its file-local symbol stream.
struct header_lexical_token final {
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
    token_kind kind = token_kind::invalid;
};
static_assert(sizeof(header_lexical_token) == 12);

// Transient physical token, no AST/WHO. Lexer leaves known_identifier empty;
// the optional post-Header prebind experiment may populate it. Names are
// interned in canonical order; hashes never replace full spelling comparison.
struct source_predecoded_token final {
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
    token_kind kind = token_kind::invalid;
#ifdef CW_SOURCE_PREBIND_PARALLEL_V2
    string_id known_identifier{};
#endif
#ifdef CW_SOURCE_PREHASH_PARALLEL_V2
    std::uint32_t lexical_hash = 0;
#endif
};
#if defined(CW_SOURCE_PREBIND_PARALLEL_V2) && defined(CW_SOURCE_PREHASH_PARALLEL_V2)
static_assert(sizeof(source_predecoded_token) == 20);
#elif defined(CW_SOURCE_PREBIND_PARALLEL_V2) || defined(CW_SOURCE_PREHASH_PARALLEL_V2)
static_assert(sizeof(source_predecoded_token) == 16);
#else
static_assert(sizeof(source_predecoded_token) == 12);
#endif

}
