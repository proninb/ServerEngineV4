#include "fixture.inc"
#include "selector.hpp"
#include <vector>
void check_links(){
 fixture f;pls10c8::observation observed;link_handle first,changed;bool replaced=false;
 status(f.G.rebind_resolved_link(f.broot,f.ref,first,replaced));require(first.value()==1&&!replaced,"First insertion flags");
 status(f.G.rebind_resolved_link(f.croot,f.ref,changed,replaced));require(changed==first&&replaced&&f.G.link_count()==1,"Replacement moved handle");
 require(f.G.find(first)->source==f.croot,"Replacement lost source");
 link_handle strict;status(f.G.add_resolved_link(f.croot,f.ref,strict));require(strict==first,"Strict identical add changed handle");
 require(!succeeded(f.G.add_resolved_link(f.broot,f.ref,strict))&&f.G.link_count()==1,"Strict add accepted conflicting source");
 changed=first;replaced=true;
 require(!succeeded(f.G.rebind_resolved_link({},f.ref,changed,replaced))&&!changed&&!replaced&&f.G.link_count()==1,"Invalid source mutated Graph");
 require(!succeeded(f.G.rebind_resolved_link(f.broot,{},changed,replaced))&&!changed&&!replaced&&f.G.link_count()==1,"Invalid target mutated Graph");
 std::vector<std::pair<object_endpoint,link_handle>> saved;
 for(unsigned i=0;i<8192;++i){const auto who=f.who(("r"+std::to_string(i)).c_str(),identity_kind::object);object_handle object;
  status(f.G.add_resolved_object(who,f.G.named(f.holder),object));const object_endpoint target{who,f.ref.member};link_handle h;
  status(f.G.rebind_resolved_link(f.broot,target,h,replaced));require(!replaced&&h.value()==i+2,"Append order changed");saved.push_back({target,h});}
 for(auto i=saved.rbegin();i!=saved.rend();++i){link_handle h;status(f.G.rebind_resolved_link(f.croot,i->first,h,replaced));require(replaced&&h==i->second&&f.G.link_at_target(i->first)==h,"Rehash/rebind changed stable handle");}
 require(f.G.link_count()==8193,"Replacement appended record");
 std::uint64_t fingerprint=1469598103934665603ULL;
 for(const auto& l:f.G.link_entries())for(auto n:{l.source.object.value(),l.source.member.value(),l.target.object.value(),l.target.member.value()})fingerprint=(fingerprint^n)*1099511628211ULL;
 std::cout<<"link_fingerprint="<<fingerprint<<",links="<<f.G.link_count()<<'\n';
}
int main(){try{check_links();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
