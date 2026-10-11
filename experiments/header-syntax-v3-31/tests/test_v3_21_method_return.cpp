// Compile with -DTEST_V321_RETURN -include tests/mock_project/mock.hpp.
// Reuse the complete independent emitter in its real compilation unit, with
// no copy of return-type parsing logic in this test.
#define main v321_unused_benchmark_main
#include "../benchmarks/header_syntax_v3_17_project_benchmark.cpp"
#undef main
#include <cassert>
#include <iostream>
using namespace cw::server;

static void check(std::initializer_list<token_kind> tokens,
    std::initializer_list<std::pair<std::size_t,string_id>> names,
    std::vector<derived_type_kind> expected,
    v319::method_return_status wanted=v319::method_return_status::emitted) {
    root_tokens root{};
    root.effective.assign(tokens);
    root.identifiers.resize(root.effective.size());
    root.positive_numbers.resize(root.effective.size());
    for(auto [index,id]:names)root.identifiers.at(index)=id;
    string_table strings;
    v319::producer_state producer{strings};
    const syntax_declaration_03 decl{0,static_cast<std::uint32_t>(root.effective.size()),0,
        syntax_decl_03::ambiguous_callable};
    v321_derived_log.clear();
    const auto result=v319::emit_method_return(producer,root,decl,
        identity_ref{1},"S",strings);
    assert(result==wanted);
    assert(v321_derived_log==expected);
    assert(producer.method_returns==(wanted==v319::method_return_status::emitted?1u:0u));
}
int main(){
    using K=token_kind;
    using D=derived_type_kind;
    // V2 has to derive pointer(char), although the conversion adds no member.
    check({K::kw_operator,K::kw_char,K::star,K::l_paren,K::r_paren,K::semicolon},
        {},{D::pointer});
    // Each named operator needs its return type but ignores parameter types.
    check({K::identifier,K::ampersand,K::kw_operator,K::assign,K::l_paren,
           K::kw_const,K::identifier,K::ampersand,K::identifier,K::r_paren,K::semicolon},
        {{0,string_id{7}},{6,string_id{7}},{8,string_id{6}}},
        {D::lvalue_reference});
    check({K::identifier,K::ampersand,K::kw_operator,K::l_bracket,K::r_bracket,
           K::l_paren,K::kw_int,K::identifier,K::r_paren,K::semicolon},
        {{0,string_id{7}},{7,string_id{6}}},
        {D::lvalue_reference});
    // A qualified operator has a return type independent of the S:: qualifier.
    check({K::kw_bool,K::identifier,K::scope,K::kw_operator,K::equal,
           K::l_paren,K::r_paren,K::semicolon},
        {{1,string_id{5}}},{});
    check({K::kw_virtual,K::kw_void,K::identifier,K::l_paren,K::r_paren,K::semicolon},
        {{2,string_id{6}}},{});
    // An 'operator' token inside a method body must not become the method name.
    check({K::kw_void,K::identifier,K::l_paren,K::r_paren,K::l_brace,
           K::kw_operator,K::kw_char,K::star,K::l_paren,K::r_paren,
           K::semicolon,K::r_brace},{{1,string_id{6}}},{});
    // V2 derives pointer and then applies pointer const.
    check({K::identifier,K::star,K::kw_const,K::identifier,K::l_paren,K::r_paren,K::semicolon},
        {{0,string_id{7}},{3,string_id{6}}},
        {D::pointer,D::const_qualified});
    check({K::identifier,K::l_paren,K::r_paren,K::semicolon},
        {{0,string_id{5}}},{},v319::method_return_status::no_return);
    check({K::tilde,K::identifier,K::l_paren,K::r_paren,K::semicolon},
        {{1,string_id{5}}},{},v319::method_return_status::no_return);
    check({K::kw_operator,K::star,K::l_paren,K::r_paren,K::semicolon},
        {},{},v319::method_return_status::invalid);
    std::cout<<"V3-21 method return tests PASS\n";
}
