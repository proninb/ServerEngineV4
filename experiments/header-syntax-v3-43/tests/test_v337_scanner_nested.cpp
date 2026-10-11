#include "benchmarks/header_syntax_v3_06.hpp"
#include <cassert>
#include <iostream>
using namespace cw::server;
int main() {
    constexpr token_kind t[]={
      token_kind::kw_struct,token_kind::identifier,token_kind::l_brace,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::kw_struct,token_kind::identifier,token_kind::l_brace,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::r_brace,token_kind::semicolon,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::r_brace,token_kind::semicolon
    };
    syntax_result_03 s;
    assert(header_syntax_v3_06::scan(t,s));
    assert(s.records.size()==2 && s.declarations.size()==3);
    assert(s.records[0].declaration_first==0 && s.records[0].declaration_count==3);
    assert(s.records[1].declaration_first==1 && s.records[1].declaration_count==1);
    assert(s.declarations[0].owner==0 && s.declarations[1].owner==1 && s.declarations[2].owner==0);
    assert(s.declarators.size()==3);
    for(unsigned i=0;i<3;++i)assert(s.declarators[i].declaration==i);
    std::cout << "V337_SCANNER_NESTED_PASS\n";
}
