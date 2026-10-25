#include "bulk.hpp"
#include <cstdio>
using namespace cw::server;
int main(){
    string_table strings;identity_space ids{strings};graph G;
    const auto integer=G.intrinsic(intrinsic_type::signed_int);
    type_ref reference;
    if(!succeeded(G.derive_resolved(integer,derived_type_kind::lvalue_reference,0,reference)))return 1;
    const auto object=[&](const char* text,type_ref type){
        string_id name;identity_ref who;object_handle handle;
        if(!succeeded(strings.intern(text,name)) || !succeeded(ids.resolve(ids.root(),name,identity_kind::object,who)) ||
           !succeeded(G.add_resolved_object(who,type,handle)))return identity_ref{};
        return who;
    };
    const auto a=object("a",integer),b=object("b",integer),r=object("r",reference);
    if(!a || !b || !r)return 2;
    endpoint_path_handle ip,rp;
    if(!succeeded(G.intern_resolved_endpoint_path(integer,{},integer,ip)) ||
       !succeeded(G.intern_resolved_endpoint_path(reference,{},reference,rp)))return 3;
    const object_endpoint ae{a,endpoint_ref::from_path(ip)},be{b,endpoint_ref::from_path(ip)},re{r,endpoint_ref::from_path(rp)};
    const auto value=[](unsigned v){return construction_value::constant(construction_kind::unsigned_integer,v);};
    bool replaced=false;link_handle original;
    if(!succeeded(G.add_resolved_initialization(ae,integer,value(7),replaced)) || replaced ||
       !succeeded(G.add_resolved_link(ae,re,original)))return 4;
    bulk_materialization01::batch batch{G};link_handle link;
    if(!succeeded(batch.add_initialization(ae,integer,value(9),replaced)) || !replaced ||
       !succeeded(batch.add_initialization(be,integer,value(4),replaced)) || replaced ||
       !succeeded(batch.rebind(be,re,link,replaced)) || !replaced || link!=original ||
       !succeeded(batch.finish()))return 5;
    const auto ii=G.initialization_entries();const auto ll=G.link_entries();
    if(ii.size()!=2 || ii[0].target!=ae || ii[0].value!=value(9) || ii[1].target!=be || ii[1].value!=value(4) ||
       ll.size()!=1 || ll[0].source!=be || ll[0].target!=re)return 6;
    if(batch.add_initialization({},integer,value(3),replaced)!=server_status::project_configuration_invalid)return 7;
    if(!succeeded(batch.finish()) || G.object_count()!=3 || G.link_count()!=1 || G.initialization_count()!=2)return 8;
    std::puts("bulk01: existing Header-prefix slots, replacements and ordered append preserved");
}
