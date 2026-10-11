#define main v3_13_benchmark_main
#include "../benchmarks/header_syntax_v3_13_project_benchmark.cpp"
#undef main
#include <cassert>
#include <array>
#include <iostream>
int main() {
  using cw::server::token_kind;
  using cw::server::intrinsic_type;
  auto test=[](std::initializer_list<token_kind> kinds,intrinsic_type expected){
    std::vector<token_kind> x(kinds);
    assert(unambiguous_builtin_type(x,0,static_cast<std::uint32_t>(x.size()))==expected);
  };
  test({token_kind::kw_bool},intrinsic_type::bool_type);
  test({token_kind::kw_void},intrinsic_type::void_type);
  test({token_kind::kw_short},intrinsic_type::signed_short);
  test({token_kind::kw_const,token_kind::kw_unsigned,token_kind::kw_long},intrinsic_type::unsigned_long);
  test({token_kind::identifier},intrinsic_type::none);
  test({token_kind::kw_int,token_kind::identifier},intrinsic_type::none);
  std::cout<<"V3-13 builtin type coverage smoke=PASS\n";
}
