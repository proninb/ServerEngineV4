#pragma once
// V3-12: grammar-specific classification overlay, no mutation of V3 descriptors.
// A parenthesized pointer/reference-to-array declarator is a data member,
// not a function declaration. Restrict the repair to the exact V3 recognized
// shape; ordinary callable declarations and direct-init syntax are unchanged.
#include "header_syntax_v3_06.hpp"
#include <span>
namespace cw::server::v312 {
[[nodiscard]] constexpr bool parenthesized_array_field(
    std::span<const token_kind> tokens,
    const syntax_declaration_03& decl,
    const syntax_declarator_08& d) noexcept {
    if (decl.kind != syntax_decl_03::ambiguous_callable ||
        (d.flags & 8u) == 0 || (d.flags & 2u) == 0 ||
        (d.flags & 4u) != 0 || d.name == 0xffffffffu ||
        decl.end > tokens.size() || decl.begin >= decl.end ||
        static_cast<std::size_t>(decl.end - decl.begin) < 8) return false;
    const auto b = static_cast<std::size_t>(decl.begin);
    return d.declaration != 0xffffffffu &&
        d.name == b + 3 && d.type_end == b + 1 &&
        tokens[b + 1] == token_kind::l_paren &&
        (tokens[b + 2] == token_kind::star ||
         tokens[b + 2] == token_kind::ampersand ||
         tokens[b + 2] == token_kind::logical_and) &&
        tokens[b + 3] == token_kind::identifier &&
        tokens[b + 4] == token_kind::r_paren &&
        tokens[b + 5] == token_kind::l_bracket &&
        d.arrays != 0;
}
} // namespace cw::server::v312
