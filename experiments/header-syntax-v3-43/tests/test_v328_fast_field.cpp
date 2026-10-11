#include "v3_28_fast_field.hpp"
#include <array>
#include <cassert>
#include <span>

using namespace cw::server;
static bool fast(std::span<const token_kind> tokens,
                 const syntax_declaration_03& decl,
                 const syntax_declarator_08& desc, bool grouped=false) {
    v316::source_view source{tokens,{},{} ,nullptr,nullptr};
    return v328_fast::simple_field_shape(source,decl,desc,grouped);
}
int main(){
    {
        const std::array ts{token_kind::kw_int,token_kind::identifier,token_kind::semicolon};
        syntax_declaration_03 decl{0,3,0,syntax_decl_03::field_candidate};
        syntax_declarator_08 d{};d.type_end=1;d.name=1;d.initializer_begin=2;
        assert(fast(ts,decl,d));
        assert(v328_fast::implicit_default(ts,2,3));
    }
    {
        const std::array ts{token_kind::kw_int,token_kind::identifier,token_kind::assign,token_kind::pp_number,token_kind::semicolon};
        syntax_declaration_03 decl{0,5,0,syntax_decl_03::field_candidate};
        syntax_declarator_08 d{};d.type_end=1;d.name=1;d.initializer_begin=2;
        assert(fast(ts,decl,d));
        assert(!v328_fast::implicit_default(ts,2,5));
    }
    {
        const std::array ts{token_kind::kw_int,token_kind::star,token_kind::identifier,token_kind::semicolon};
        syntax_declaration_03 decl{0,4,0,syntax_decl_03::field_candidate};
        syntax_declarator_08 d{};d.type_end=1;d.name=2;d.initializer_begin=3;
        assert(!fast(ts,decl,d));
    }
    {
        const std::array ts{token_kind::kw_int,token_kind::identifier,token_kind::l_bracket,token_kind::pp_number,token_kind::r_bracket,token_kind::semicolon};
        syntax_declaration_03 decl{0,6,0,syntax_decl_03::field_candidate};
        syntax_declarator_08 d{};d.type_end=1;d.name=1;d.initializer_begin=5;
        assert(!fast(ts,decl,d));
        assert(!fast(ts,decl,d,true));
    }
    {
        const std::array ts{token_kind::kw_int,token_kind::identifier,token_kind::semicolon};
        syntax_declaration_03 decl{0,3,0,syntax_decl_03::field_candidate};
        syntax_declarator_08 d{};d.type_end=1;d.name=0xffffffffu;d.initializer_begin=2;
        assert(!fast(ts,decl,d));
        assert(!v328_fast::implicit_default(ts,4,3));
        assert(!v328_fast::implicit_default(ts,0,4));
        assert(v328_fast::implicit_default(ts,3,3));
    }
}
