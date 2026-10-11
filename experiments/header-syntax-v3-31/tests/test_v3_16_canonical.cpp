#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include <cassert>
#include <array>
#include <iostream>
using namespace cw::server;
int main() {
    string_table strings;
    identity_space ids(strings);
    graph G;
    // `void * field;` versus V2 Graph pointer-to-void
    std::array<token_kind,4> kinds{token_kind::kw_void,token_kind::star,token_kind::identifier,token_kind::semicolon};
    std::array<string_id,4> ids_src{};
    std::array<std::uint64_t,4> num{};
    v316::source_view stream{kinds,ids_src,num};
    auto x=v316::verify(stream,0,1,2,4,4,false,ids.root(),ids,G,
        type_ref{type_ref_kind::derived,1},intrinsic_type::void_type);
#ifdef TEST_BAD_SHAPE
    assert(x.result==v316::verdict::mismatch && x.why==v316::reason::layer_mismatch);
#else
#if defined(TEST_ARRAY7) || defined(TEST_ARRAY8)
    assert(x.result==v316::verdict::mismatch);
#else
    assert(x.result==v316::verdict::match);
#endif
#endif
    kinds[0]=token_kind::kw_const;
    x=v316::verify(stream,0,1,2,4,4,false,ids.root(),ids,G,
        type_ref{type_ref_kind::derived,1},intrinsic_type::none);
    assert(x.result==v316::verdict::not_checked);
    // Reject unhandled syntax instead of falsely matching V2.
    kinds[0]=token_kind::kw_void;kinds[1]=token_kind::kw_operator;
    x=v316::verify(stream,0,1,2,4,4,false,ids.root(),ids,G,
        type_ref{type_ref_kind::derived,1},intrinsic_type::void_type);
    assert(x.result==v316::verdict::not_checked);
    // Check the true V2 bounded-array payload, not only array count.
    std::array<token_kind,6> array_kinds{token_kind::kw_void,token_kind::identifier,
        token_kind::l_bracket,token_kind::pp_number,token_kind::r_bracket,token_kind::semicolon};
    std::array<string_id,6> array_symbols{};
    std::array<std::uint64_t,6> array_numbers{0,0,0,7,0,0};
    v316::source_view arr{array_kinds,array_symbols,array_numbers};
    auto array_result=v316::verify(arr,0,1,1,6,6,false,ids.root(),ids,G,
        type_ref{type_ref_kind::derived,1},intrinsic_type::void_type);
#if defined(TEST_ARRAY7) || defined(TEST_BAD_SHAPE)
    assert(array_result.result==v316::verdict::match);
#elif defined(TEST_ARRAY8)
    assert(array_result.result==v316::verdict::mismatch && array_result.why==v316::reason::layer_mismatch);
#else
    assert(array_result.result==v316::verdict::mismatch);
#endif
    // Canonical names must resolve via identity_space, never by borrowing V2 base.
    std::array<token_kind,3> named_kinds{token_kind::identifier,token_kind::identifier,token_kind::semicolon};
    std::array<string_id,3> named_symbols{string_id{7},string_id{6},string_id{}};
    std::array<std::uint64_t,3> named_numbers{};
    v316::source_view named{named_kinds,named_symbols,named_numbers};
    auto named_result=v316::verify(named,0,1,1,3,3,false,ids.root(),ids,G,
        type_ref{type_ref_kind::named,7},intrinsic_type::none);
#ifdef TEST_ALIAS
    assert(named_result.result==v316::verdict::mismatch &&
        named_result.why==v316::reason::base_mismatch);
    named_result=v316::verify(named,0,1,1,3,3,false,ids.root(),ids,G,
        type_ref{type_ref_kind::intrinsic,static_cast<uint32_t>(intrinsic_type::unsigned_char)},intrinsic_type::none);
    assert(named_result.result==v316::verdict::match);
#else
    assert(named_result.result==v316::verdict::match);
#endif
    named_symbols[0]=string_id{100};
    named_result=v316::verify(named,0,1,1,3,3,false,ids.root(),ids,G,
        type_ref{type_ref_kind::named,7},intrinsic_type::none);
    assert(named_result.result==v316::verdict::not_checked &&
        named_result.why==v316::reason::unresolved_name &&
        named_result.unresolved_token==0 &&
        named_result.lookup_scope==ids.root().value() &&
        named_result.lookup_steps>0);
    std::cout << "V3-16 canonical smoke=PASS\n";
}
