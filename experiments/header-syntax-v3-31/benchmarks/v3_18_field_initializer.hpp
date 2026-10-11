#pragma once
// V3-18 independent, source-token-only field-default initializer decoder.
// No Graph reads, no inference from authoritative construction values.
#include "v3_17_constructor.hpp"
#include <span>
#include <string>

namespace cw::server::v318 {
struct field_result {
    bool complete = false;
    bool explicit_initializer = false;
    v317::operation value{};
    std::string reason;
};
template<class Identifier,class Number>
[[nodiscard]] field_result parse_field(std::span<const token_kind> ts,
    std::size_t begin,std::size_t end, Identifier identifier,Number number_at) {
    field_result out;
    if(end>ts.size() || begin>end) {out.reason="invalid_range";return out;}
    // V3-06 uses declaration.end as sentinel when there is no initializer.
    if(begin==end || (begin+1==end && ts[begin]==token_kind::semicolon)) {
        out.complete=true;return out;
    }
    const std::size_t terminal=ts[end-1]==token_kind::semicolon?end-1:end;
    if(begin>=terminal) {out.complete=true;return out;}
    auto pos=begin;
    if(ts[pos]!=token_kind::assign && ts[pos]!=token_kind::l_brace){
        out.reason="unsupported_prefix";return out;
    }
    const auto first=ts[pos++];
    bool braced=first==token_kind::l_brace;
    if(!braced && pos<terminal && ts[pos]==token_kind::l_brace){braced=true;++pos;}
    if(braced && pos<terminal && ts[pos]==token_kind::r_brace){
        ++pos;
        if(pos==terminal) {out.explicit_initializer=true;out.complete=true;return out;}
        out.reason="trailing_brace_tokens";return out;
    }
    if(pos>=terminal){out.reason="missing_operand";return out;}
    bool neg=false;
    if(ts[pos]==token_kind::minus || ts[pos]==token_kind::plus){
        neg=ts[pos]==token_kind::minus;++pos;
    }
    auto& value=out.value;
    if(pos>=terminal){out.reason="missing_operand";return out;}
    if(ts[pos]==token_kind::kw_false || ts[pos]==token_kind::kw_nullptr){
        if(neg){out.reason="negative_bool";return out;}
        ++pos;
    }else if(ts[pos]==token_kind::kw_true){
        if(neg){out.reason="negative_true";return out;}
        value.kind=v317::operand_kind::unsigned_integer;
        value.bits=1;++pos;
    }else if(ts[pos]==token_kind::pp_number){
        const auto n=number_at(pos);
        if(n.kind==v317::number_kind::none){out.reason="invalid_number";return out;}
        if(n.kind==v317::number_kind::real){
            value.kind=v317::operand_kind::real;
            value.bits=n.bits^(neg?(std::uint64_t{1}<<63):0ull);
        }else{
            value.kind=neg?v317::operand_kind::signed_integer:v317::operand_kind::unsigned_integer;
            value.bits=neg?std::uint64_t{0}-n.bits:n.bits;
        }
        ++pos;
    }else{
        if(neg){out.reason="negative_reference";return out;}
        if(ts[pos]==token_kind::kw_this){
            ++pos;
            if(pos>=terminal || ts[pos++]!=token_kind::dot){out.reason="this_dot";return out;}
        }
        if(pos>=terminal || ts[pos]!=token_kind::identifier || !identifier(pos)){
            out.reason="operand_unsupported";return out;
        }
        value.kind=v317::operand_kind::member_reference;
        value.reference=identifier(pos++);
    }
    if(braced){
        if(pos>=terminal || ts[pos++]!=token_kind::r_brace){out.reason="missing_close_brace";return out;}
    }
    if(pos!=terminal){out.reason="trailing_operand";return out;}
    out.complete=true;out.explicit_initializer=true;
    return out;
}
}
