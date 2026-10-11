#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include "v3_16_operators.hpp"
#include <array>
#include <cassert>
#include <iostream>
using namespace cw::server;
using namespace v316;

int main() {
    string_table strings;
    identity_space ids(strings);
    graph G;
    constexpr auto il = static_cast<std::uint32_t>(intrinsic_type::signed_long_long);
    constexpr auto ul = static_cast<std::uint32_t>(intrinsic_type::unsigned_long_long);
    constexpr auto b = static_cast<std::uint32_t>(intrinsic_type::bool_type);

    // Contextual token is exactly an identifier, NOT a reserved keyword.
    std::array<token_kind,3> plain {token_kind::identifier,token_kind::identifier,token_kind::semicolon};
    std::array<string_id,3> sym {string_id{37},string_id{6},string_id{}};
    std::array<std::uint64_t,3> numbers{};
    source_view src{plain,sym,numbers,&strings,&strings};
    auto check = [&](type_ref expected) {return verify(src,0,1,1,3,3,false,ids.root(),ids,G,expected,intrinsic_type::none);};
    assert(check(type_ref{type_ref_kind::intrinsic,il}).result == verdict::match);
    auto fail = check(type_ref{type_ref_kind::intrinsic,b});
    assert(fail.result==verdict::mismatch && fail.why==reason::base_mismatch);

    std::array<token_kind,4> sign {token_kind::kw_unsigned,token_kind::identifier,token_kind::identifier,token_kind::semicolon};
    std::array<string_id,4> ss {string_id{},string_id{37},string_id{6},string_id{}};
    std::array<std::uint64_t,4> num{};
    source_view ssrc{sign,ss,num,&strings,&strings};
    auto signed_case = [&](type_ref expected) {return verify(ssrc,0,2,2,4,4,false,ids.root(),ids,G,expected,intrinsic_type::none);};
    assert(signed_case(type_ref{type_ref_kind::intrinsic,ul}).result==verdict::match);
    sign[0]=token_kind::kw_signed;
    assert(signed_case(type_ref{type_ref_kind::intrinsic,il}).result==verdict::match);
    sign[0]=token_kind::kw_long;
    assert(signed_case(type_ref{type_ref_kind::intrinsic,il}).result==verdict::not_checked);

    std::array<token_kind,5> bad {token_kind::kw_unsigned,token_kind::kw_signed,token_kind::identifier,token_kind::identifier,token_kind::semicolon};
    std::array<string_id,5> badsym {string_id{},string_id{},string_id{37},string_id{6},string_id{}};
    std::array<std::uint64_t,5> badnum{};
    source_view badsrc{bad,badsym,badnum,&strings,&strings};
    auto invalid=verify(badsrc,0,3,3,5,5,false,ids.root(),ids,G,
        type_ref{type_ref_kind::intrinsic,il},intrinsic_type::none);
    assert(invalid.result==verdict::not_checked && invalid.why==reason::unsupported_base);

    // Array/pointer/cv: base and ordered layer reconstruction must be preserved.
    std::array<token_kind,4> cv{token_kind::kw_const,token_kind::identifier,token_kind::star,token_kind::identifier};
    std::array<string_id,4> cvsym{string_id{},string_id{37},string_id{},string_id{6}};
    std::array<std::uint64_t,4> cvnum{};
    source_view cvsrc{cv,cvsym,cvnum,&strings,&strings};
    type_spec parsed{};
    assert(resolve_base(cvsrc,0,2,ids.root(),ids,G,intrinsic_type::none,parsed));
    assert(parsed.base_kind==type_ref_kind::intrinsic && parsed.base_payload==il);
    assert(parsed.length==1 && parsed.layers[0].kind==derived_type_kind::const_qualified);

    using K=token_kind;
    const std::array<K,5> assign{K::identifier,K::kw_operator,K::assign,K::l_paren,K::r_paren};
    assert(classify_operator(assign,0,assign.size())==operator_form::assignment);
    const std::array<K,5> subscript{K::kw_operator,K::l_bracket,K::r_bracket,K::l_paren,K::r_paren};
    assert(classify_operator(subscript,0,subscript.size())==operator_form::subscript);
    const std::array<K,5> conversion{K::kw_operator,K::kw_char,K::star,K::l_paren,K::r_paren};
    assert(classify_operator(conversion,0,conversion.size())==operator_form::conversion);
    const std::array<K,5> call{K::kw_operator,K::l_paren,K::r_paren,K::l_paren,K::r_paren};
    assert(classify_operator(call,0,call.size())==operator_form::call);
    const std::array<K,3> method{K::kw_int,K::identifier,K::l_paren};
    assert(classify_operator(method,0,method.size())==operator_form::not_operator);
    const std::array<K,3> truncated{K::kw_operator,K::l_bracket,K::identifier};
    assert(classify_operator(truncated,0,truncated.size())==operator_form::unsupported);
    std::cout << "V3-16 contextual_intrinsic_and_operator_tests=PASS\n";
}
