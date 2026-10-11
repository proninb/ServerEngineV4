#pragma once
// V3-16 targeted no-name classifier. Shadow audit only; no Graph-oracle labels.
#include "project/frontend/lexical_token.hpp"
#include <cstdint>
#include <span>
#include <string_view>

namespace v316 {
using cw::server::token_kind;
enum class operator_form : std::uint8_t {
    not_operator, conversion, assignment, subscript, call, symbolic,
    new_delete, unsupported
};
[[nodiscard]] inline std::string_view operator_form_name(operator_form f) noexcept {
    switch(f) {
    case operator_form::not_operator:return "not_operator";
    case operator_form::conversion:return "conversion";
    case operator_form::assignment:return "operator_assignment";
    case operator_form::subscript:return "operator_subscript";
    case operator_form::call:return "operator_call";
    case operator_form::symbolic:return "operator_symbolic";
    case operator_form::new_delete:return "operator_new_delete";
    case operator_form::unsupported:return "operator_unsupported";
    }
    return "invalid_operator_form";
}
[[nodiscard]] inline operator_form classify_operator(
    std::span<const token_kind> t,std::uint32_t begin,std::uint32_t end) noexcept {
    if(begin>=end||end>t.size())return operator_form::not_operator;
    for(std::uint32_t at=begin;at<end;++at) {
        if(t[at]!=token_kind::kw_operator)continue;
        const auto i=at+1;
        if(i>=end)return operator_form::unsupported;
        const auto name=t[i];
        if(name==token_kind::assign)return operator_form::assignment;
        if(name==token_kind::l_bracket)return i+1<end && t[i+1]==token_kind::r_bracket?
            operator_form::subscript:operator_form::unsupported;
        if(name==token_kind::l_paren)return i+1<end && t[i+1]==token_kind::r_paren?
            operator_form::call:operator_form::unsupported;
        if(name==token_kind::kw_new||name==token_kind::kw_delete)
            return operator_form::new_delete;
        switch(name) {
        case token_kind::identifier:case token_kind::kw_void:
        case token_kind::kw_bool:case token_kind::kw_char:
        case token_kind::kw_wchar_t:case token_kind::kw_char8_t:
        case token_kind::kw_char16_t:case token_kind::kw_char32_t:
        case token_kind::kw_short:case token_kind::kw_int:
        case token_kind::kw_long:case token_kind::kw_signed:
        case token_kind::kw_unsigned:case token_kind::kw_float:
        case token_kind::kw_double:case token_kind::kw_const:
        case token_kind::kw_volatile:case token_kind::scope:
            return operator_form::conversion;
        default:break;
        }
        switch(name) {
        case token_kind::plus:case token_kind::minus:case token_kind::star:
        case token_kind::slash:case token_kind::percent:case token_kind::caret:
        case token_kind::ampersand:case token_kind::pipe:case token_kind::tilde:
        case token_kind::exclamation:case token_kind::equal:case token_kind::not_equal:
        case token_kind::less:case token_kind::greater:case token_kind::less_equal:
        case token_kind::greater_equal:case token_kind::logical_and:
        case token_kind::logical_or:case token_kind::plus_plus:
        case token_kind::minus_minus:case token_kind::arrow:case token_kind::arrow_star:
        case token_kind::comma:case token_kind::shift_left:case token_kind::shift_right:
        case token_kind::plus_assign:case token_kind::minus_assign:
        case token_kind::star_assign:case token_kind::slash_assign:
        case token_kind::percent_assign:case token_kind::ampersand_assign:
        case token_kind::pipe_assign:case token_kind::caret_assign:
        case token_kind::shift_left_assign:case token_kind::shift_right_assign:
        case token_kind::spaceship:
            return operator_form::symbolic;
        default:return operator_form::unsupported;
        }
    }
    return operator_form::not_operator;
}
} // namespace v316
