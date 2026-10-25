#include "project/graph/graph.hpp"
#include "project/graph/member_index_policy.hpp"
#include "project/semantic/identity.hpp"
#include "project/string/string_table.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cw::server;
void require(bool ok){if(!ok)throw std::runtime_error("member index growth contract failed");}
int main(){try{
    for(bool reference:{true,false}){
        detail::member_index_growth_reference=reference;
        detail::fail_next_wide_allocation=!reference;
        string_table strings;identity_space ids{strings};graph G;
        struct saved {type_handle type;graph_record_kind family;std::vector<member_record> members;};
        std::vector<saved> records;
        const auto name=[&](const std::string& text){string_id id;require(succeeded(strings.intern(text,id)));return id;};
        for(unsigned r=0;r<96;++r){
            identity_ref who;require(succeeded(ids.resolve(ids.root(),name("R"+std::to_string(r)),identity_kind::type,who)));
            saved record;record.family=r%3?graph_record_kind::struct_type:graph_record_kind::class_type;
            require(succeeded(G.declare_record(who,record.family,record.type)));
            const auto count=r%11==0?0:(r*37)%129+1;
            for(unsigned j=0;j<count;++j)record.members.push_back({name("m"+std::to_string(j)),G.intrinsic(intrinsic_type::signed_int),graph_member_access::public_access});
            const auto status=r%2?G.define_record(record.type,record.family,record.members):G.define_resolved_record(record.type,record.family,record.members);
            require(succeeded(status));records.push_back(std::move(record));
            // Earlier records must survive every intermediate growth.
            for(const auto& old:records){for(std::size_t j=0;j<old.members.size();++j){
                const auto found=G.find_member(old.type,old.members[j].name);require(bool(found)&&found.value()==j);
                require(G.member(old.type,found)->name==old.members[j].name);
            }require(!G.find_member(old.type,name("absent")));}
        }
        const auto total=G.member_count();
        for(auto& record:records){
            require(succeeded(G.define_resolved_record(record.type,record.family,record.members)));
            if(!record.members.empty()){auto changed=record.members;changed[0].name=name("changed");
                require(!succeeded(G.define_resolved_record(record.type,record.family,changed)));
                require(G.find_member(record.type,record.members[0].name).value()==0);
                require(!G.find_member(record.type,changed[0].name));}
        }
        require(G.member_count()==total);std::cout<<"reference="<<reference<<",records="<<records.size()<<",members="<<total<<",validation=passed\n";
        require(!detail::fail_next_wide_allocation);
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
