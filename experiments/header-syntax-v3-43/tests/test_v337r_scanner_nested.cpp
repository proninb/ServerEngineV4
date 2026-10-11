#include "benchmarks/header_syntax_v3_06.hpp"
#include "benchmarks/v337r_owner_ranges.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace cw::server;
static void require(bool value,const char* name){if(!value)throw std::runtime_error(name);}
int main(){
 try {
    constexpr token_kind t[]={
      token_kind::kw_struct,token_kind::identifier,token_kind::l_brace,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::kw_struct,token_kind::identifier,token_kind::l_brace,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::kw_struct,token_kind::identifier,token_kind::l_brace,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::r_brace,token_kind::semicolon,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::r_brace,token_kind::semicolon,
      token_kind::kw_int,token_kind::identifier,token_kind::semicolon,
      token_kind::r_brace,token_kind::semicolon
    };
    syntax_result_03 s;
    require(header_syntax_v3_06::scan(t,s),"scan");
    require(s.records.size()==3,"records");
    require(s.declarations.size()==5,"declarations");
    require(s.records[0].declaration_count==5,"parent DFS count");
    require(s.records[1].declaration_count==3,"child DFS count");
    require(v337r_ranges::owned_declaration_count(s,0)==2,"parent owns two");
    require(v337r_ranges::owned_declaration_count(s,1)==2,"child owns two");
    require(v337r_ranges::owned_declaration_count(s,2)==1,"grandchild owns one");
    require(s.declarators.size()==5,"descriptors");
    for(std::size_t i=0;i<s.declarators.size();++i){
        require(s.declarators[i].declaration==i,"monotonic unique descriptor");
        require(s.declarators[i].declaration<s.declarations.size(),"bounded descriptor");
    }
    std::cout<<"V337R_SCANNER_NESTED_PASS\n";
    return EXIT_SUCCESS;
 }catch(const std::exception& e){std::cerr<<"V337R_SCANNER_NESTED_FAIL "<<e.what()<<'\n';return EXIT_FAILURE;}
}
