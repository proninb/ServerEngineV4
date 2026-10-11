#pragma once
// V3-26: optional fast path for a simple unqualified named field base.
// Cache is per RECORD, keyed by native source string_id. Type lookups are
// never taken from the V2 oracle; cache contents are always V3-derived.
// Qualified / contextual / complex types fall back to V3-25 untouched.
#include "v3_16_canonical.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace v326_type {
using namespace cw::server;
class base_type_cache final {
public:
    struct entry final {
        std::uint32_t epoch=0;
        std::uint32_t payload=0;
        type_ref_kind kind=type_ref_kind::invalid;
    };
    void begin_record() noexcept {
        if (++epoch_==0) {
            for (auto& item:entries_)item.epoch=0;
            epoch_=1;
        }
    }
    [[nodiscard]] bool get(std::uint32_t id,type_ref_kind& kind,std::uint32_t& payload) const noexcept {
        if (!id || id>=entries_.size())return false;
        const auto& item=entries_[id];
        if(item.epoch!=epoch_ || item.kind==type_ref_kind::invalid)return false;
        kind=item.kind;payload=item.payload;return true;
    }
    void put(std::uint32_t id,type_ref_kind kind,std::uint32_t payload) {
        constexpr std::size_t limit=32u*1024u*1024u;
        if(!id || id>=limit)return;
        if(id>=entries_.size()){
            auto capacity=std::max<std::size_t>(256u,entries_.size());
            while(capacity<=id)capacity=std::min(capacity*2,limit);
            entries_.resize(capacity);
        }
        entries_[id]={epoch_,payload,kind};
    }
#ifdef CW_V326_TYPE_TEST
    void test_epoch(std::uint32_t e) noexcept {epoch_=e;}
#endif
private:
    std::vector<entry> entries_;
    std::uint32_t epoch_=0;
};

// One-pass common intrinsic spelling: cv* SINGLE_BUILTIN cv*.
// Multi-token signed/unsigned/long/short forms use the complete V3-25 decoder.
[[nodiscard]] inline bool try_resolve_single_intrinsic(
    const v316::source_view& src,std::uint32_t first,std::uint32_t last,
    v316::type_spec& output) noexcept {
    if(first>=last||last>src.tokens.size())return false;
    bool c=false,v=false;
    intrinsic_type builtin=intrinsic_type::none;
    for(auto p=first;p<last;++p){
        intrinsic_type next=intrinsic_type::none;
        switch(src.tokens[p]) {
        case token_kind::kw_const:c=true;continue;
        case token_kind::kw_volatile:v=true;continue;
        case token_kind::kw_int:next=intrinsic_type::signed_int;break;
        case token_kind::kw_void:next=intrinsic_type::void_type;break;
        case token_kind::kw_bool:next=intrinsic_type::bool_type;break;
        case token_kind::kw_char:next=intrinsic_type::char_type;break;
        case token_kind::kw_float:next=intrinsic_type::float_type;break;
        case token_kind::kw_double:next=intrinsic_type::double_type;break;
        case token_kind::kw_wchar_t:next=intrinsic_type::wchar_type;break;
        case token_kind::kw_char8_t:next=intrinsic_type::char8_type;break;
        case token_kind::kw_char16_t:next=intrinsic_type::char16_type;break;
        case token_kind::kw_char32_t:next=intrinsic_type::char32_type;break;
        default:return false;
        }
        if(builtin!=intrinsic_type::none)return false;
        builtin=next;
    }
    if(builtin==intrinsic_type::none)return false;
    output.base_kind=type_ref_kind::intrinsic;
    output.base_payload=static_cast<std::uint32_t>(builtin);
    return (!c||output.push(derived_type_kind::const_qualified)) &&
           (!v||output.push(derived_type_kind::volatile_qualified));
}

// Returns false to USE the original V3-25 resolver, not to accept a type.
// Fast grammar deliberately permits only cv* IDENTIFIER cv*, no namespace,
// no __int64 contextual identifier, no signed/unsigned or other modifier.
[[nodiscard]] inline bool try_resolve_simple(
    const v316::source_view& src,std::uint32_t first,std::uint32_t last,
    identity_ref owner,const identity_space& ids,const graph& G,
    base_type_cache& cache,v316::type_spec& output,
    std::uint64_t* hits=nullptr,std::uint64_t* misses=nullptr) {
    if(first>=last || last>src.tokens.size() || last>src.symbols.size())return false;
    bool c=false,v=false;
    std::uint32_t symbol_at=last;
    for(auto p=first;p<last;++p){
        switch(src.tokens[p]){
        case token_kind::kw_const:c=true;break;
        case token_kind::kw_volatile:v=true;break;
        case token_kind::identifier:
            if(symbol_at!=last)return false;
            symbol_at=p;break;
        default:return false;
        }
    }
    if(symbol_at==last || !src.symbols[symbol_at])return false;
    // Same lexical special case as Header V2; the fallback handles sign and cv.
    if(src.v3_strings && src.v3_strings->get(src.symbols[symbol_at])=="__int64")return false;
    type_ref_kind kind=type_ref_kind::invalid;
    std::uint32_t payload=0;
    const auto id=src.symbols[symbol_at].value();
    if(cache.get(id,kind,payload)){
        if(hits)++*hits;
    }else{
        if(misses)++*misses;
        v316::type_spec trace;
        const auto resolved=v316::lookup_type(ids,owner,src.v2_symbol(symbol_at),trace,symbol_at);
        if(!resolved)return false;
        const auto handle=G.find_type(resolved);
        const auto* entry=handle?G.find(handle):nullptr;
        if(entry && entry->kind==graph_type_kind::intrinsic_alias){
            kind=type_ref_kind::intrinsic;
            payload=static_cast<std::uint32_t>(entry->alias_intrinsic());
        }else{
            kind=type_ref_kind::named;
            payload=resolved.slot();
        }
        cache.put(id,kind,payload);
    }
    output.base_kind=kind;
    output.base_payload=payload;
    return (!c || output.push(derived_type_kind::const_qualified)) &&
           (!v || output.push(derived_type_kind::volatile_qualified));
}
} // namespace v326_type
