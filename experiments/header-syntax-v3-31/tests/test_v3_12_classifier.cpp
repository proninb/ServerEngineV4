#include "v3_12_classification.hpp"
#include "header_syntax_v3_04_reference.hpp"
#include <array>
#include <cassert>
#include <iostream>
using namespace cw::server;
int main() {
    // struct S { bool (&x)[7]; bool (*p)[7]; int m(); int value(7); S(); };
    constexpr token_kind input[] = {
        token_kind::kw_struct, token_kind::identifier, token_kind::l_brace,
        token_kind::kw_bool, token_kind::l_paren, token_kind::ampersand,
        token_kind::identifier, token_kind::r_paren, token_kind::l_bracket,
        token_kind::identifier, token_kind::r_bracket, token_kind::semicolon,
        token_kind::kw_bool, token_kind::l_paren, token_kind::star,
        token_kind::identifier, token_kind::r_paren, token_kind::l_bracket,
        token_kind::identifier, token_kind::r_bracket, token_kind::semicolon,
        token_kind::kw_int, token_kind::identifier, token_kind::l_paren,
        token_kind::r_paren, token_kind::semicolon,
        token_kind::kw_int, token_kind::identifier, token_kind::l_paren,
        token_kind::identifier, token_kind::r_paren, token_kind::semicolon,
        token_kind::identifier, token_kind::l_paren, token_kind::r_paren,
        token_kind::semicolon,
        token_kind::r_brace, token_kind::semicolon,
    };
    syntax_result_03 v3;
    v304_reference::syntax_result_03 ref;
    assert(header_syntax_v3_06::scan(input, v3));
    assert(v304_reference::header_syntax_v3_04::scan(input, ref));
    assert(v3.declarations.size() == ref.declarations.size());
    assert(v3.declarators.size() == ref.declarators.size());
    unsigned fixed=0, left_callable=0;
    for (const auto& d:v3.declarators) {
        const auto& decl=v3.declarations.at(d.declaration);
        const bool is_array=v312::parenthesized_array_field(input,decl,d);
        if(is_array) {
            ++fixed;
            assert((d.flags & 8u) != 0);
            assert(decl.kind == syntax_decl_03::ambiguous_callable);
            assert(d.name == decl.begin+3);
        } else if(decl.kind == syntax_decl_03::ambiguous_callable ||
                  decl.kind == syntax_decl_03::method_candidate) ++left_callable;
    }
    assert(fixed==2);
    assert(left_callable==3);
    std::cout<<"v312_classifier=PASS reclassified="<<fixed
             <<" left_callable="<<left_callable<<'\n';
}
