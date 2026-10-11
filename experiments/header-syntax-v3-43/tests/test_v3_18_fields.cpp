#include "server_engine/project/frontend/lexical_token.hpp"
#include "v3_18_field_initializer.hpp"
#include <array>
#include <cassert>
#include <cstdint>
using namespace cw::server;
using enum token_kind;
int main(){
 auto ident=[](std::size_t i)->std::uint32_t {return static_cast<std::uint32_t>(i+1);};
 auto num=[](std::size_t i)->v317::number{return {i+7,v317::number_kind::unsigned_integer};};
 {
  constexpr std::array t{kw_int,identifier,semicolon};
  auto r=v318::parse_field(t,3,3,ident,num);
  assert(r.complete&&!r.explicit_initializer&&r.value.kind==v317::operand_kind::zero);
 }
 {
  constexpr std::array t{kw_int,identifier,assign,pp_number,semicolon};
  auto r=v318::parse_field(t,2,5,ident,num);
  assert(r.complete&&r.explicit_initializer&&r.value.bits==10);
 }
 {
  constexpr std::array t{kw_int,identifier,l_brace,minus,pp_number,r_brace,semicolon};
  auto r=v318::parse_field(t,2,7,ident,num);
  assert(r.complete&&r.value.kind==v317::operand_kind::signed_integer);
  assert(r.value.bits==std::uint64_t{0}-11);
 }
 {
  constexpr std::array t{kw_int,identifier,assign,l_brace,r_brace,semicolon};
  auto r=v318::parse_field(t,2,6,ident,num);
  assert(r.complete&&r.explicit_initializer&&r.value.kind==v317::operand_kind::zero);
 }
 {
  constexpr std::array t{kw_int,ampersand,identifier,assign,kw_this,dot,identifier,semicolon};
  auto r=v318::parse_field(t,3,8,ident,num);
  assert(r.complete&&r.value.kind==v317::operand_kind::member_reference&&r.value.reference==7);
 }
 {
  constexpr std::array t{kw_int,identifier,assign,identifier,plus,identifier,semicolon};
  auto r=v318::parse_field(t,2,7,ident,num);
  assert(!r.complete&&r.reason=="trailing_operand");
 }
 {
  constexpr std::array t{kw_int,identifier,assign,kw_true,semicolon};
  auto r=v318::parse_field(t,2,5,ident,num);
  assert(r.complete&&r.value.bits==1);
 }
}
