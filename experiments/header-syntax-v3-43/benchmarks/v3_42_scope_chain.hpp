#pragma once
// V3-31: record-local lexical scope chain for successful simple named type
// lookup. Not a global symbol cache, never caches failures; semantic lookup
// still goes through identity_space::find(scope,name,type). All fallback
// cases continue using V3-27.
#include "v3_42_fast_type.hpp"
#include <array>
#include <chrono>
#include <cstdint>

namespace v342_scope {
using namespace cw::server;
using clock=std::chrono::steady_clock;
using ticks=clock::time_point;
inline double ms(ticks first) noexcept {
  return std::chrono::duration<double,std::milli>(clock::now()-first).count();
}
struct named_profile final {
  double parse_ms=0,cache_probe_ms=0,scope_lookup_ms=0,alias_classify_ms=0;
  std::uint64_t name_attempts=0,cache_hits=0,cache_misses=0,
      scope_steps=0,aliases=0,ordinary_named=0,lookup_failures=0;
};
class scope_chain final {
public:
  void begin_record(const identity_space& ids,identity_ref owner) noexcept {
    count_=0; owner_=owner; valid_=true;
    auto cursor=owner;
    for(unsigned depth=0;cursor && depth<256;++depth) {
      chain_[count_++]=cursor;
      identity_record rec;
      if(!ids.record(cursor,rec)) {valid_=false;return;}
      cursor=rec.parent;
    }
    // The original lookup_type stops after 256 scopes. Same limit here.
  }
  [[nodiscard]] identity_ref lookup(const identity_space& ids,identity_ref owner,
      string_id name,std::uint64_t* steps=nullptr) const noexcept {
    if(!valid_ || owner.value()!=owner_.value()) {
      // Defensive fallback: never accidentally use a chain for another owner.
      v342_canonical::type_spec trace;
      const auto r=v342_canonical::lookup_type(ids,owner,name,trace,0);
      if(steps)*steps+=trace.lookup_steps;
      return r;
    }
    for(std::size_t i=0;i<count_;++i) {
      if(steps)++*steps;
      if(const auto r=ids.find(chain_[i],name,identity_kind::type);r)return r;
    }
    return {};
  }
  [[nodiscard]] std::size_t depth() const noexcept {return count_;}
private:
  std::array<identity_ref,256> chain_{};
  identity_ref owner_{};
  std::size_t count_=0;
  bool valid_=false;
};
// Identical grammar restrictions, cache semantics and cv ordering to V3-26.
[[nodiscard]] inline bool try_resolve_simple(
  const v342_canonical::source_view& src,std::uint32_t first,std::uint32_t last,
  identity_ref owner,const identity_space& ids,const graph& G,
  v342_type::base_type_cache& cache,const scope_chain& scopes,
  v342_canonical::type_spec& output,std::uint64_t* hits=nullptr,
  std::uint64_t* misses=nullptr,named_profile* p=nullptr) {
  const auto parse_start=p?clock::now():ticks{};
  if(first>=last || last>src.tokens.size() || last>src.symbols.size())return false;
  bool c=false,v=false;
  std::uint32_t symbol_at=last;
  for(auto pos=first;pos<last;++pos){
    switch(src.tokens[pos]){
      case token_kind::kw_const:c=true;break;
      case token_kind::kw_volatile:v=true;break;
      case token_kind::identifier:
        if(symbol_at!=last)return false;
        symbol_at=pos;break;
      default:return false;
    }
  }
  if(symbol_at==last || !src.symbols[symbol_at])return false;
  if(src.v3_strings && src.v3_strings->get(src.symbols[symbol_at])=="__int64")return false;
  if(p){p->parse_ms+=ms(parse_start);++p->name_attempts;}
  type_ref_kind kind=type_ref_kind::invalid;
  std::uint32_t payload=0;
  const auto id=src.symbols[symbol_at].value();
  const auto probe_start=p?clock::now():ticks{};
  const bool cached=cache.get(id,kind,payload);
  if(p){p->cache_probe_ms+=ms(probe_start);if(cached)++p->cache_hits;else ++p->cache_misses;}
  if(cached){if(hits)++*hits;}
  else {
    if(misses)++*misses;
    const auto lookup_start=p?clock::now():ticks{};
    std::uint64_t steps=0;
    const auto resolved=scopes.lookup(ids,owner,src.v2_symbol(symbol_at),p?&steps:nullptr);
    if(p){p->scope_lookup_ms+=ms(lookup_start);p->scope_steps+=steps;}
    if(!resolved){if(p)++p->lookup_failures;return false;}
    const auto alias_start=p?clock::now():ticks{};
    const auto handle=G.find_type(resolved);
    const auto* entry=handle?G.find(handle):nullptr;
    if(entry && entry->kind==graph_type_kind::intrinsic_alias){
      kind=type_ref_kind::intrinsic;
      payload=static_cast<std::uint32_t>(entry->alias_intrinsic());
      if(p)++p->aliases;
    }else{
      kind=type_ref_kind::named;
      payload=resolved.slot();
      if(p)++p->ordinary_named;
    }
    cache.put(id,kind,payload);
    if(p)p->alias_classify_ms+=ms(alias_start);
  }
  output.base_kind=kind;
  output.base_payload=payload;
  return (!c || output.push(derived_type_kind::const_qualified)) &&
         (!v || output.push(derived_type_kind::volatile_qualified));
}
} // namespace v331_type
