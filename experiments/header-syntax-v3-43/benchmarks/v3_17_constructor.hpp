#pragma once
// Experimental V3-17 constructor-token decoding, independent of Header V2's
// semantic parser. No Graph access, no source replay, no production changes.
#include "project/frontend/lexical_token.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cw::server::v317 {

enum class number_kind : std::uint8_t { none, unsigned_integer, real };
struct number { std::uint64_t bits{}; number_kind kind{number_kind::none}; };
enum class operand_kind : std::uint8_t { zero, signed_integer, unsigned_integer, real, member_reference };
struct step { std::uint32_t name = 0; std::uint64_t index = 0; bool array = false; };
struct operation {
    std::uint32_t target = 0;
    std::vector<step> path;
    operand_kind kind = operand_kind::zero;
    std::uint64_t bits = 0;
    std::uint32_t reference = 0;
    std::uint32_t token = 0;
};
struct result {
    std::vector<operation> operations;
    std::string reason;
    bool complete = false;
    bool declaration_only = false;
};
// Input readers are strongly typed lambdas: identifier(token_index) -> uint32_t,
// number(token_index) -> v317::number. No Graph identifiers are supplied.
template<class Identifier, class Number>
[[nodiscard]] result parse(std::span<const token_kind> tokens,
    std::size_t begin, std::size_t end, Identifier identifier, Number number_at) {
    result r;
    if (begin >= end || end > tokens.size()) { r.reason="invalid_range"; return r; }
    auto at=[&](std::size_t i,token_kind k){return i<end && tokens[i]==k;};
    auto fail=[&](const char* reason){r.reason=reason;return r;};
    std::size_t pos=begin;
    if (!at(pos,token_kind::identifier) || !identifier(pos)) return fail("invalid_constructor_name");
    ++pos;
    if (!at(pos++,token_kind::l_paren) || !at(pos++,token_kind::r_paren))
        return fail("parameters_unsupported");
    if(at(pos,token_kind::kw_noexcept))++pos;
    if(at(pos,token_kind::semicolon) && pos+1==end) {
        r.complete=true; r.declaration_only=true;return r;
    }
    if(at(pos,token_kind::assign) && at(pos+1,token_kind::kw_default) &&
       at(pos+2,token_kind::semicolon) && pos+3==end) {
        r.complete=true;r.declaration_only=true;return r;
    }
    auto parse_operand=[&](operation& op,token_kind close)->bool {
        if(at(pos,close)) {op.kind=operand_kind::zero; return true;}
        bool negative=false;
        if(at(pos,token_kind::minus)||at(pos,token_kind::plus)){
            negative=at(pos,token_kind::minus);++pos;
        }
        if(at(pos,token_kind::kw_false)||at(pos,token_kind::kw_nullptr)){
            if(negative)return false;
            op.kind=operand_kind::zero;++pos;return true;
        }
        if(at(pos,token_kind::kw_true)){
            if(negative)return false;
            op.kind=operand_kind::unsigned_integer;op.bits=1;++pos;return true;
        }
        if(at(pos,token_kind::pp_number)){
            const auto n=number_at(pos);
            if(n.kind==number_kind::none)return false;
            if(n.kind==number_kind::real){
                op.kind=operand_kind::real;
                op.bits=n.bits^(negative?(std::uint64_t{1}<<63):0ull);
            }else {
                op.kind=negative?operand_kind::signed_integer:operand_kind::unsigned_integer;
                op.bits=negative?std::uint64_t{0}-n.bits:n.bits;
            }
            ++pos;return true;
        }
        if(negative)return false;
        if(at(pos,token_kind::kw_this)){
            ++pos;if(!at(pos++,token_kind::dot))return false;
        }
        if(at(pos,token_kind::identifier) && identifier(pos)){
            op.kind=operand_kind::member_reference;op.reference=identifier(pos);
            ++pos;return true;
        }
        return false;
    };
    // Managed initializer-list: target(expr), target{expr}, ...
    if(at(pos,token_kind::colon)){
        ++pos;
        for(;;){
            operation op;op.token=static_cast<std::uint32_t>(pos);
            if(!at(pos,token_kind::identifier)||!(op.target=identifier(pos)))
                return fail("initializer_target");
            ++pos;
            token_kind close=token_kind::invalid;
            if(at(pos,token_kind::l_paren))close=token_kind::r_paren;
            else if(at(pos,token_kind::l_brace))close=token_kind::r_brace;
            else return fail("initializer_open");
            ++pos;
            if(!parse_operand(op,close)||!at(pos,close))return fail("initializer_operand");
            ++pos;
            r.operations.push_back(std::move(op));
            if(!at(pos,token_kind::comma))break;
            ++pos;
        }
    }
    if(!at(pos,token_kind::l_brace))return fail("body_open");
    ++pos;
    while(!at(pos,token_kind::r_brace)){
        operation op;op.token=static_cast<std::uint32_t>(pos);
        if(!at(pos,token_kind::identifier)||!(op.target=identifier(pos)))
            return fail("body_target");
        ++pos;
        while(at(pos,token_kind::dot)||at(pos,token_kind::l_bracket)){
            if(op.path.size()>=256)return fail("path_depth");
            if(at(pos,token_kind::dot)){
                ++pos;
                if(!at(pos,token_kind::identifier)||!identifier(pos))return fail("path_field");
                op.path.push_back({identifier(pos),0,false});++pos;
            }else {
                ++pos;
                if(!at(pos,token_kind::pp_number))return fail("path_index");
                auto n=number_at(pos);
                if(n.kind!=number_kind::unsigned_integer)return fail("path_index");
                op.path.push_back({0,n.bits,true});++pos;
                if(!at(pos,token_kind::r_bracket))return fail("path_bracket");
                ++pos;
            }
        }
        if(!at(pos,token_kind::assign))return fail("body_assign");
        ++pos;
        if(!parse_operand(op,token_kind::semicolon)||!at(pos,token_kind::semicolon))
            return fail("body_operand");
        ++pos;r.operations.push_back(std::move(op));
    }
    ++pos;
    if(pos<end && at(pos,token_kind::semicolon))++pos;
    if(pos!=end)return fail("trailing_tokens");
    r.complete=true;
    return r;
}
} // namespace cw::server::v317
