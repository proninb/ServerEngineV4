// Standalone C++20 tests for the V3-only constructor decoder. No Graph oracle.
#include "server_engine/project/frontend/lexical_token.hpp"
#include "v3_17_constructor.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
using namespace cw::server;
using enum token_kind;

int main(){
    auto ident=[](std::size_t pos)->std::uint32_t{return static_cast<std::uint32_t>(pos+1);};
    auto number=[](std::size_t pos)->v317::number{return {pos+10,v317::number_kind::unsigned_integer};};
    {
        // C(): a(5), b{true} { a=7; }
        constexpr std::array t{identifier,l_paren,r_paren,colon,
            identifier,l_paren,pp_number,r_paren,comma,
            identifier,l_brace,kw_true,r_brace,l_brace,
            identifier,assign,pp_number,semicolon,r_brace};
        const auto r=v317::parse(t,0,t.size(),ident,number);
        assert(r.complete && !r.declaration_only && r.operations.size()==3);
        assert(r.operations[0].kind==v317::operand_kind::unsigned_integer && r.operations[0].bits==16);
        assert(r.operations[1].kind==v317::operand_kind::unsigned_integer && r.operations[1].bits==1);
        assert(r.operations[2].bits==26);
    }
    {
        // C(){ target.inner[2]=-3; ref=this.member; }
        constexpr std::array t{identifier,l_paren,r_paren,l_brace,
            identifier,dot,identifier,l_bracket,pp_number,r_bracket,assign,minus,pp_number,semicolon,
            identifier,assign,kw_this,dot,identifier,semicolon,r_brace};
        const auto r=v317::parse(t,0,t.size(),ident,number);
        assert(r.complete && r.operations.size()==2);
        assert(r.operations[0].path.size()==2 && r.operations[0].path[1].array);
        assert(r.operations[0].kind==v317::operand_kind::signed_integer);
        assert(r.operations[0].bits==std::uint64_t{0}-22);
        assert(r.operations[1].kind==v317::operand_kind::member_reference);
        assert(r.operations[1].reference==19);
    }
    {
        constexpr std::array t{identifier,l_paren,r_paren,semicolon};
        const auto r=v317::parse(t,0,t.size(),ident,number);
        assert(r.complete && r.declaration_only && r.operations.empty());
    }
    {
        constexpr std::array t{identifier,l_paren,r_paren,assign,kw_default,semicolon};
        const auto r=v317::parse(t,0,t.size(),ident,number);
        assert(r.complete && r.declaration_only);
    }
    {
        constexpr std::array t{identifier,l_paren,identifier,r_paren,semicolon};
        const auto r=v317::parse(t,0,t.size(),ident,number);
        assert(!r.complete && r.reason=="parameters_unsupported");
    }
    {
        constexpr std::array t{identifier,l_paren,r_paren,l_brace,identifier,assign,pp_number,semicolon,r_brace};
        auto real=[](std::size_t)->v317::number {return {0x8000000000000000ULL,v317::number_kind::real};};
        const auto r=v317::parse(t,0,t.size(),ident,real);
        assert(r.complete && r.operations[0].kind==v317::operand_kind::real && r.operations[0].bits==0x8000000000000000ULL);
    }
    {
        constexpr std::array t{identifier,l_paren,r_paren,l_brace,kw_return,semicolon,r_brace};
        const auto r=v317::parse(t,0,t.size(),ident,number);
        assert(!r.complete && r.reason=="body_target");
    }
}
