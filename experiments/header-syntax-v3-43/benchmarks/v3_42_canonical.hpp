#pragma once
// V3-16: conservative, read-only reconstruction of a canonical field type.
// Zero 'matches' are inferred from Graph fields. A type is checked only when
// its source grammar and base WHO resolve independently from effective tokens.
#include "project/graph/graph.hpp"
#include "project/semantic/identity.hpp"
#include "project/frontend/lexical_token.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace v342_canonical {
using namespace cw::server;
struct layer { derived_type_kind kind; std::uint64_t payload; };
enum class verdict { match, mismatch, not_checked };
enum class reason {
    none, invalid_span, unsupported_base, unresolved_name, ambiguous_qualified_name,
    unsupported_declarator, unsupported_array_bound, too_deep, invalid_graph_chain,
    base_mismatch, layer_mismatch
};
inline constexpr std::string_view reason_name(reason r) noexcept {
    switch(r) {
    case reason::none:return "none";
    case reason::invalid_span:return "invalid_span";
    case reason::unsupported_base:return "unsupported_base";
    case reason::unresolved_name:return "unresolved_name";
    case reason::ambiguous_qualified_name:return "ambiguous_qualified_name";
    case reason::unsupported_declarator:return "unsupported_declarator";
    case reason::unsupported_array_bound:return "unsupported_array_bound";
    case reason::too_deep:return "too_deep";
    case reason::invalid_graph_chain:return "invalid_graph_chain";
    case reason::base_mismatch:return "base_mismatch";
    case reason::layer_mismatch:return "layer_mismatch";
    }
    return "invalid_reason";
}
struct outcome {
    verdict result=verdict::not_checked;
    reason why=reason::unsupported_base;
    std::uint32_t expected_base=0, actual_base=0;
    std::uint32_t expected_layer_count=0, actual_layer_count=0;
    std::uint32_t first_difference=0;
    std::uint32_t unresolved_token=0xffffffffu;
    std::uint32_t lookup_scope=0;
    std::uint32_t lookup_steps=0;
};
struct source_view {
    std::span<const token_kind> tokens;
    std::span<const string_id> symbols;
    // Zero indicates absent, invalid or non-positive integer literal.
    // Value is from the production prepared_token::number.bits, not source text.
    std::span<const std::uint64_t> positive_numbers;
    // V3 effective tokens and V2 Parser use different string_table instances.
    // Translate by spelling; NEVER pass V3 string_id directly to V2 identity_space.
    const string_table* v3_strings=nullptr;
    const string_table* v2_strings=nullptr;
    [[nodiscard]] string_id v2_symbol(std::uint32_t i) const noexcept {
        if(i>=symbols.size())return {};
        if(!v3_strings||!v2_strings)return symbols[i]; // isolated mock/unit tests
        const auto spelling=v3_strings->get(symbols[i]);
        return spelling.empty()?string_id{}:v2_strings->find(spelling);
    }
};
struct type_spec {
    type_ref_kind base_kind=type_ref_kind::invalid;
    std::uint32_t base_payload=0;
    std::array<layer,32> layers; // inner to outer; only [0,length) is initialized by push()
    std::uint32_t length=0;
    reason failure=reason::none;
    std::uint32_t unresolved_token=0xffffffffu;
    std::uint32_t lookup_scope=0;
    std::uint32_t lookup_steps=0;
    bool push(derived_type_kind kind,std::uint64_t payload=0) noexcept {
        if(length==layers.size()) {failure=reason::too_deep;return false;}
        layers[length++]={kind,payload};return true;
    }
};
inline identity_ref lookup_type(const identity_space& ids,identity_ref scope,string_id name,
        type_spec& trace, std::uint32_t name_token) noexcept {
    trace.unresolved_token=name_token;
    trace.lookup_scope=scope.value();
    for(unsigned depth=0; scope && depth<256; ++depth) {
        ++trace.lookup_steps;
        if(const auto found=ids.find(scope,name,identity_kind::type); found)return found;
        identity_record node;
        if(!ids.record(scope,node))return {};
        scope=node.parent;
    }
    return {};
}
inline bool resolve_base(const source_view& src,std::uint32_t first,std::uint32_t last,
    identity_ref owner,const identity_space& ids,const graph& G,
    intrinsic_type builtin,type_spec& dst) {
    if(last>src.tokens.size()||last>src.symbols.size()||first>=last) {
        dst.failure=reason::invalid_span;return false;
    }
    bool cv_const=false,cv_volatile=false;
    // The Header V2 grammar canonicalizes `const` then `volatile`.
    std::array<std::uint32_t,16> names{};
    std::uint32_t count=0;
    bool scope_expected=false;
    bool global_qualified=false;
    bool signed_modifier=false, unsigned_modifier=false;
    bool unsupported_contextual_modifier=false;
    for(std::uint32_t i=first;i<last;++i) {
        switch(src.tokens[i]) {
        case token_kind::kw_const:cv_const=true;break;
        case token_kind::kw_volatile:cv_volatile=true;break;
        case token_kind::kw_signed:
            if(signed_modifier||unsigned_modifier) {dst.failure=reason::unsupported_base;return false;}
            signed_modifier=true;break;
        case token_kind::kw_unsigned:
            if(unsigned_modifier||signed_modifier) {dst.failure=reason::unsupported_base;return false;}
            unsigned_modifier=true;break;
        case token_kind::kw_short:case token_kind::kw_long:
            unsupported_contextual_modifier=true;break;
        case token_kind::identifier:
            if(count==names.size()||(count>0&&!scope_expected)) {dst.failure=reason::unsupported_base;return false;}
            names[count++]=i;break;
        case token_kind::scope:
            if(count==0 && i==first) {global_qualified=true;break;}
            if(count==0||scope_expected||i+1==last) {dst.failure=reason::unsupported_base;return false;}
            scope_expected=true;break;
        case token_kind::kw_void:case token_kind::kw_bool:case token_kind::kw_char:
        case token_kind::kw_wchar_t:case token_kind::kw_char8_t:case token_kind::kw_char16_t:
        case token_kind::kw_char32_t:case token_kind::kw_float:case token_kind::kw_double:
        case token_kind::kw_int:
            if(count) {dst.failure=reason::unsupported_base;return false;}
            break;
        default:dst.failure=reason::unsupported_base;return false;
        }
        if(src.tokens[i]==token_kind::identifier)scope_expected=false;
    }
    if(scope_expected) {dst.failure=reason::unsupported_base;return false;}
    // MSVC __int64 is a contextual intrinsic in production Header V2.
    // It is token_kind::identifier; NEVER resolve it through identity_space.
    // Mirror Header V2 sign and long/short restrictions. This branch does
    // not read the actual Graph field type and cannot create a false match.
    const bool contextual_int64 = count==1 && !global_qualified &&
        src.v3_strings && src.v3_strings->get(src.symbols[names[0]])=="__int64";
    if(contextual_int64) {
        if(unsupported_contextual_modifier || builtin!=intrinsic_type::none) {
            dst.failure=reason::unsupported_base;return false;
        }
        dst.base_kind=type_ref_kind::intrinsic;
        dst.base_payload=static_cast<std::uint32_t>(unsigned_modifier?
            intrinsic_type::unsigned_long_long:intrinsic_type::signed_long_long);
    } else if(builtin!=intrinsic_type::none && count==0) {
        dst.base_kind=type_ref_kind::intrinsic;
        dst.base_payload=static_cast<std::uint32_t>(builtin);
    } else if(count) {
        if(builtin!=intrinsic_type::none||signed_modifier||unsigned_modifier||
            unsupported_contextual_modifier) {dst.failure=reason::unsupported_base;return false;}
        identity_ref resolved;
        if(count==1 && !global_qualified) {
            resolved=lookup_type(ids,owner,src.v2_symbol(names[0]),dst,names[0]);
        } else {
            dst.unresolved_token=names[count-1];
            identity_ref scope=global_qualified?ids.root():owner;
            dst.lookup_scope=scope.value();
            // Unqualified paths may be relative to owner and ancestor scopes.
            // Reject rather than guess if the exact scope cannot be established.
            if(!global_qualified && count>1) {
                dst.failure=reason::ambiguous_qualified_name;return false;
            }
            for(std::uint32_t index=0;index<count;++index) {
                ++dst.lookup_steps;
                if(index+1==count)resolved=ids.find(scope,src.v2_symbol(names[index]),identity_kind::type);
                else {
                    auto next=ids.find(scope,src.v2_symbol(names[index]),identity_kind::namespace_scope);
                    if(!next)next=ids.find(scope,src.v2_symbol(names[index]),identity_kind::type);
                    if(!next) {dst.failure=reason::unresolved_name;return false;}
                    scope=next;
                }
            }
        }
        if(!resolved) {dst.failure=reason::unresolved_name;return false;}
        // Header V2 TYPEDEF-07: intrinsic aliases are scalar types in G.
        // Do not canonicalize ordinary record aliases to an intrinsic.
        const auto handle=G.find_type(resolved);
        if(handle) {
            const auto* entry=G.find(handle);
            if(entry && entry->kind==graph_type_kind::intrinsic_alias) {
                dst.base_kind=type_ref_kind::intrinsic;
                dst.base_payload=static_cast<std::uint32_t>(entry->alias_intrinsic());
            } else {
                dst.base_kind=type_ref_kind::named;
                dst.base_payload=resolved.slot();
            }
        } else {
            // Preserve V2 incomplete-name semantics; the structural Graph
            // comparison will fail rather than invent a complete type.
            dst.base_kind=type_ref_kind::named;
            dst.base_payload=resolved.slot();
        }
    } else {dst.failure=reason::unsupported_base;return false;}
    if(cv_const && !dst.push(derived_type_kind::const_qualified))return false;
    if(cv_volatile && !dst.push(derived_type_kind::volatile_qualified))return false;
    return true;
}

// Only a deliberately narrow subset of declarator grammar is certified:
// base type, *, *const, *volatile, &, &&, and [positive decimal].
// Includes grouped pointer/reference to bounded arrays `T (*p)[N]`.
inline outcome verify(const source_view& src,std::uint32_t begin,std::uint32_t type_end,
    std::uint32_t name,std::uint32_t initializer_begin,std::uint32_t declaration_end,
    bool grouped_array, identity_ref owner,const identity_space& ids,
    const graph& G,type_ref graph_type,intrinsic_type builtin) {
    outcome out;
    const auto n=src.tokens.size();
    if(n!=src.symbols.size()||n!=src.positive_numbers.size()||
       !(begin<type_end && type_end<=name && name<declaration_end && declaration_end<=n)) {
        out.why=reason::invalid_span;return out;
    }
    type_spec expected;
    if(!resolve_base(src,begin,type_end,owner,ids,G,builtin,expected)) {
        out.why=expected.failure;
        out.unresolved_token=expected.unresolved_token;
        out.lookup_scope=expected.lookup_scope;
        out.lookup_steps=expected.lookup_steps;
        return out;
    }
    const auto k=src.tokens;
    std::array<layer,32> grouped{};
    unsigned grouped_count=0;
    std::uint32_t prefix=type_end;
    if(grouped_array) {
        // `T (&name)[N]` and `T (*name)[N]` have a wrapper OUTSIDE the arrays.
        if(prefix+2!=name || k[prefix]!=token_kind::l_paren ||
           (k[prefix+1]!=token_kind::star && k[prefix+1]!=token_kind::ampersand &&
            k[prefix+1]!=token_kind::logical_and)) {
            out.why=reason::unsupported_declarator;return out;
        }
        grouped[grouped_count++]={k[prefix+1]==token_kind::star?derived_type_kind::pointer:
            k[prefix+1]==token_kind::ampersand?derived_type_kind::lvalue_reference:
            derived_type_kind::rvalue_reference,0};
        prefix=name;
    }
    while(prefix<name) {
        const auto current=k[prefix++];
        if(current!=token_kind::star && current!=token_kind::ampersand &&
           current!=token_kind::logical_and) {
            out.why=reason::unsupported_declarator;return out;
        }
        const auto kind=current==token_kind::star?derived_type_kind::pointer:
            current==token_kind::ampersand?derived_type_kind::lvalue_reference:
            derived_type_kind::rvalue_reference;
        if(!expected.push(kind)) {out.why=expected.failure;return out;}
        bool pointer_const=false,pointer_volatile=false;
        if(kind==derived_type_kind::pointer) {
            while(prefix<name && (k[prefix]==token_kind::kw_const || k[prefix]==token_kind::kw_volatile)) {
                pointer_const|=k[prefix]==token_kind::kw_const;
                pointer_volatile|=k[prefix]==token_kind::kw_volatile;
                ++prefix;
            }
            if(pointer_const && !expected.push(derived_type_kind::const_qualified)) {out.why=expected.failure;return out;}
            if(pointer_volatile && !expected.push(derived_type_kind::volatile_qualified)) {out.why=expected.failure;return out;}
        }
    }
    std::uint32_t at=name+1;
    if(grouped_array) {
        if(at>=declaration_end||k[at]!=token_kind::r_paren) {
            out.why=reason::unsupported_declarator;return out;
        }
        ++at;
    }
    std::array<std::uint64_t,32> bounds{};
    unsigned bounds_count=0;
    const auto stop=std::min(initializer_begin,declaration_end);
    while(at<stop && k[at]==token_kind::l_bracket) {
        if(at+2>=stop || k[at+1]!=token_kind::pp_number ||
           !src.positive_numbers[at+1] || k[at+2]!=token_kind::r_bracket ||
           bounds_count==bounds.size()) {
            out.why=reason::unsupported_array_bound;return out;
        }
        bounds[bounds_count++]=src.positive_numbers[at+1];
        at+=3;
    }
    // An initializer or `;` ends the declarator; any other intervening syntax
    // is not reconstructed by this restricted type checker.
    if(at<stop && k[at]!=token_kind::semicolon && k[at]!=token_kind::comma) {
        out.why=reason::unsupported_declarator;return out;
    }
    if(grouped_array && !bounds_count) {
        out.why=reason::unsupported_array_bound;return out;
    }
    for(unsigned i=bounds_count;i>0;--i) {
        if(!expected.push(derived_type_kind::bounded_array,bounds[i-1])) {
            out.why=expected.failure;return out;
        }
    }
    for(unsigned i=0;i<grouped_count;++i) {
        if(!expected.push(grouped[i].kind)) {out.why=expected.failure;return out;}
    }
    out.expected_base=(static_cast<std::uint32_t>(expected.base_kind)<<30)|expected.base_payload;
    out.expected_layer_count=expected.length;
    std::array<layer,32> actual{}; // outer to inner
    unsigned count=0;
    type_ref cursor=graph_type;
    while(cursor && cursor.kind()==type_ref_kind::derived) {
        if(count==actual.size()) {out.why=reason::too_deep;return out;}
        derived_type_record node;
        if(!G.derived(cursor,node)||!node.child) {out.why=reason::invalid_graph_chain;return out;}
        actual[count++]={node.kind,node.payload};
        cursor=node.child;
    }
    if(!cursor) {out.why=reason::invalid_graph_chain;return out;}
    out.actual_base=cursor.value();
    out.actual_layer_count=count;
    if(cursor.kind()!=expected.base_kind || cursor.payload()!=expected.base_payload) {
        out.result=verdict::mismatch;out.why=reason::base_mismatch;return out;
    }
    if(count!=expected.length) {
        out.result=verdict::mismatch;out.why=reason::layer_mismatch;return out;
    }
    for(unsigned i=0;i<count;++i) {
        const auto& a=actual[i], b=expected.layers[count-i-1];
        if(a.kind!=b.kind||a.payload!=b.payload) {
            out.result=verdict::mismatch;out.why=reason::layer_mismatch;
            out.first_difference=i;return out;
        }
    }
    out.result=verdict::match;out.why=reason::none;return out;
}
}
