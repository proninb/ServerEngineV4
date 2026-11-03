#pragma once
#include "../../server_engine/project/graph/graph.hpp"
#include <algorithm>
#include <cstdlib>
#include <ostream>
#include <span>
#include <vector>
namespace pls13 {
inline const bool enabled=[] {auto p=std::getenv("PLS10_C13_REUSE");return !p||*p!='0';}();
struct counts {std::uint64_t statements=0,eligible=0,hits=0,misses=0,keys=0,large=0,allocation_failures=0,verified=0;};
inline counts audit;
inline void count(std::uint64_t& n){
#ifdef PLS13_AUDIT
 ++n;
#else
 (void)n;
#endif
}
// One last-prefix memo, not a dictionary/index. No WHO key and no inferred slots.
class last_paths {
 using step=cw::server::endpoint_path_step;using handle=cw::server::endpoint_path_handle;
 cw::server::type_ref root{},resolved{},wanted{};std::vector<step> prefix;std::vector<handle> handles;
 std::uint64_t bound=0;bool valid=false,active=false,failed=false;
public:
 struct scope {last_paths& owner;scope(last_paths& p,std::uint64_t n,cw::server::type_ref child):owner(p){owner.active=enabled&&n<=256;owner.bound=n;owner.wanted=child;owner.failed=false;count(audit.statements);if(enabled&&n>256)count(audit.large);}scope(const scope&)=delete;scope& operator=(const scope&)=delete;~scope(){owner.active=false;}};
 [[nodiscard]] scope begin(std::uint64_t n,cw::server::type_ref child){return scope{*this,n,child};}
 template<class T> handle* lookup(const T& t){
  if(!active||failed||t.type!=wanted||t.steps.empty()||t.steps.back().kind!=cw::server::endpoint_path_step_kind::array_index||t.steps.back().value>=bound)return nullptr;
  count(audit.eligible);const auto path=std::span<const step>{t.steps}.first(t.steps.size()-1);
  if(!valid||root!=t.root||resolved!=t.type||handles.size()!=bound||prefix.size()!=path.size()||!std::equal(prefix.begin(),prefix.end(),path.begin())){
   valid=false;try {prefix.assign(path.begin(),path.end());handles.assign(static_cast<std::size_t>(bound),{});root=t.root;resolved=t.type;valid=true;count(audit.keys);}
   catch(...){failed=true;count(audit.allocation_failures);return nullptr;}
  }
  auto& h=handles[static_cast<std::size_t>(t.steps.back().value)];if(h)count(audit.hits);else count(audit.misses);return &h;
 }
};
inline void print(std::ostream& o){o<<"\npls13_enabled="<<enabled<<",pls13_statements="<<audit.statements<<",pls13_eligible="<<audit.eligible<<",pls13_hits="<<audit.hits<<",pls13_misses="<<audit.misses<<",pls13_keys="<<audit.keys<<",pls13_large="<<audit.large<<",pls13_allocation_failures="<<audit.allocation_failures<<",pls13_verified="<<audit.verified;}
}
