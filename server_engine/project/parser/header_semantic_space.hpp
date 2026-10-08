/*
 * HEADER-SEMANTIC-SPACE-01
 *
 * Small semantic control space for the preprocessed Header token stream.
 *
 * This is intentionally not an AST and not a persistent semantic graph.
 * It is the finite control component of the Header semantic transducer:
 *
 *     (state, token-class, registers) -> (state, semantic action)
 *
 * Token payload (identifier/string/source location) remains in semantic_token;
 * names and types are registers owned by the parser. The classifier collapses
 * C++ spelling tokens that are equivalent for top-level Header dispatch.
 */
#pragma once

#include "../frontend/lexer.hpp"

#include <cstdint>

namespace cw::server {

enum class header_scope_symbol : std::uint8_t {
    end_of_stream,
    close_scope,
    empty_declaration,
    typedef_declaration,
    namespace_declaration,
    record_declaration,
    object_declaration,
    other,
};

enum class header_scope_action : std::uint8_t {
    finish_root,
    fail_unclosed_scope,
    close_scope,
    skip_empty,
    parse_typedef,
    parse_namespace,
    parse_record,
    parse_object,
    reject,
};

[[nodiscard]] constexpr header_scope_symbol
classify_header_scope_symbol(
    token_kind kind,
    bool object_start) noexcept {
    switch (kind) {
    case token_kind::invalid:
        return header_scope_symbol::end_of_stream;

    case token_kind::r_brace:
        return header_scope_symbol::close_scope;

    case token_kind::semicolon:
        return header_scope_symbol::empty_declaration;

    case token_kind::kw_typedef:
        return header_scope_symbol::typedef_declaration;

    case token_kind::kw_namespace:
        return header_scope_symbol::namespace_declaration;

    case token_kind::kw_struct:
    case token_kind::kw_class:
    case token_kind::kw_union:
        return header_scope_symbol::record_declaration;

    default:
        return object_start
            ? header_scope_symbol::object_declaration
            : header_scope_symbol::other;
    }
}

[[nodiscard]] constexpr header_scope_action
header_scope_transition(
    bool expect_close,
    header_scope_symbol symbol) noexcept {

    switch (symbol) {
    case header_scope_symbol::end_of_stream:
        return expect_close
            ? header_scope_action::fail_unclosed_scope
            : header_scope_action::finish_root;

    case header_scope_symbol::close_scope:
        return expect_close
            ? header_scope_action::close_scope
            : header_scope_action::reject;

    case header_scope_symbol::empty_declaration:
        return header_scope_action::skip_empty;

    case header_scope_symbol::typedef_declaration:
        return header_scope_action::parse_typedef;

    case header_scope_symbol::namespace_declaration:
        return header_scope_action::parse_namespace;

    case header_scope_symbol::record_declaration:
        return header_scope_action::parse_record;

    case header_scope_symbol::object_declaration:
        return header_scope_action::parse_object;

    case header_scope_symbol::other:
        return header_scope_action::reject;
    }

    return header_scope_action::reject;
}

static_assert(
    classify_header_scope_symbol(token_kind::kw_struct, false) ==
    header_scope_symbol::record_declaration);
static_assert(
    classify_header_scope_symbol(token_kind::kw_class, false) ==
    header_scope_symbol::record_declaration);
static_assert(
    classify_header_scope_symbol(token_kind::kw_int, true) ==
    header_scope_symbol::object_declaration);
static_assert(
    classify_header_scope_symbol(token_kind::identifier, true) ==
    header_scope_symbol::object_declaration);
static_assert(
    header_scope_transition(
        false,
        header_scope_symbol::end_of_stream) ==
    header_scope_action::finish_root);
static_assert(
    header_scope_transition(
        true,
        header_scope_symbol::end_of_stream) ==
    header_scope_action::fail_unclosed_scope);
static_assert(
    header_scope_transition(
        true,
        header_scope_symbol::close_scope) ==
    header_scope_action::close_scope);
static_assert(
    header_scope_transition(
        false,
        header_scope_symbol::close_scope) ==
    header_scope_action::reject);

} // namespace cw::server
