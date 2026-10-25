#pragma once
#include "../../server_engine/project/graph/graph.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

// Reuse the existing experiment-only friend declaration, without RG01 hooks.
// No Graph fields, record layouts, or production methods are added.
namespace cw::server::resolved_g01 {
struct access {
    static std::uint64_t key(object_endpoint t) noexcept { return graph::link_target_key(t); }
    static bool scalar(const graph& g,type_ref t) noexcept { return g.scalar_initialization_target(t); }
    static std::size_t inits(const graph& g) noexcept {return g.object_initializations.size();}
    static std::size_t links(const graph& g) noexcept {return g.links.size();}
    static std::uint32_t find_init(const graph& g,object_endpoint t) noexcept {
        return g.find_initialization_position(t);
    }
    static link_handle find_link(const graph& g,object_endpoint t) noexcept {return g.find_link_target(t);}
    static link_handle handle(std::uint32_t slot) noexcept {
        static_assert(sizeof(link_handle)==sizeof(slot));return std::bit_cast<link_handle>(slot);
    }
    static bool valid_init(const graph& g,std::uint32_t slot,object_endpoint t) noexcept {
        return slot && slot<=g.object_initializations.size() && g.object_initializations[slot-1].target==t;
    }
    static bool valid_link(const graph& g,std::uint32_t slot,object_endpoint t) noexcept {
        return slot && slot<=g.links.size() && g.links[slot-1].target==t;
    }
    static bool verify(const graph& g) noexcept {
        if(g.initialization_target_index_count!=g.object_initializations.size() || g.link_target_index_count!=g.links.size())return false;
        for(std::size_t i=0;i<g.object_initializations.size();++i)
            if(g.find_initialization_position(g.object_initializations[i].target)!=i+1)return false;
        for(std::size_t i=0;i<g.links.size();++i)
            if(g.find_link_target(g.links[i].target).value()!=i+1)return false;
        return true;
    }
    template<class V> static void reserve(V& v,std::size_t n) {
        if(n>v.capacity())v.reserve((std::max)(n,(std::min)(v.max_size(),v.capacity()*2)));
    }
    static server_status prepare(graph& g,std::size_t ni,std::size_t nl) noexcept {
        const auto si=ni?g.ensure_initialization_target_index_capacity(ni):server_status::success;
        if(!succeeded(si))return si;
        const auto sl=nl?g.ensure_link_target_index_capacity(nl):server_status::success;
        if(!succeeded(sl))return sl;
        try {reserve(g.object_initializations,g.object_initializations.size()+ni);
             reserve(g.links,g.links.size()+nl);}
        catch(...){return server_status::io_error;}
        return server_status::success;
    }
    template<class I,class L> static void apply(graph& g,const I& ii,const L& ll,
        std::size_t ni,std::size_t nl) noexcept {
        const auto oldi=g.object_initializations.size(),oldl=g.links.size();
        g.object_initializations.resize(oldi+ni);g.links.resize(oldl+nl);
        for(const auto& v:ii){
            g.object_initializations[v.slot-1]=v.value;
            if(v.slot>oldi){g.insert_initialization_target_index(g.initialization_target_index,key(v.value.target),v.slot);
                ++g.initialization_target_index_count;}
        }
        for(const auto& v:ll){
            g.links[v.slot-1]=v.value;
            if(v.slot>oldl){g.insert_link_target_index(g.link_target_index,key(v.value.target),handle(v.slot));
                ++g.link_target_index_count;}
        }
    }
};
}

namespace cw::server::bulk_materialization01 {
using access=resolved_g01::access;
inline std::uint64_t hash(std::uint64_t v) noexcept {
    v^=v>>30;v*=0xbf58476d1ce4e5b9ULL;v^=v>>27;v*=0x94d049bb133111ebULL;return v^(v>>31);
}
struct slot {std::uint64_t key=0;std::uint32_t index=0,generation=0;};
class index {
    std::vector<slot> slots;
    std::uint32_t generation=1;
    std::size_t count=0;
public:
    std::size_t bytes() const noexcept {return slots.capacity()*sizeof(slot);}
    std::uint32_t find(std::uint64_t key,std::uint64_t h) const noexcept {
        if(slots.empty())return 0;
        auto p=static_cast<std::size_t>(h)&(slots.size()-1);
        while(slots[p].generation==generation){
            if(slots[p].key==key)return slots[p].index;
            p=(p+1)&(slots.size()-1);
        }
        return 0;
    }
    void insert(std::uint64_t key,std::uint64_t h,std::uint32_t record) {
        if(slots.empty() || count+1>slots.size()/2){
            std::vector<slot> next(slots.empty()?8:slots.size()*2);
            for(const auto& s:slots)if(s.generation==generation){
                auto p=static_cast<std::size_t>(hash(s.key))&(next.size()-1);
                while(next[p].generation==generation)p=(p+1)&(next.size()-1);
                next[p]=s;
            }
            slots.swap(next);
        }
        auto p=static_cast<std::size_t>(h)&(slots.size()-1);
        while(slots[p].generation==generation)p=(p+1)&(slots.size()-1);
        slots[p]={key,record,generation};++count;
    }
    void clear() noexcept {
        count=0;
        if(++generation==0){for(auto& s:slots)s={};generation=1;}
    }
};
struct init {object_initialization_record value{};std::uint32_t slot=0;};
struct link {link_record value{};std::uint32_t slot=0;};

class batch {
    graph& G;
    index init_index,link_index;
    std::vector<init> initializations;
    std::vector<link> links;
    std::size_t new_inits=0,new_links=0,limit=65536;
    bool failed=false,reported=false;
    std::uint64_t init_calls=0,link_calls=0,flushes=0,replacements=0,peak_pending=0,peak_bytes=0;
    static bool selected() noexcept {const auto* p=std::getenv("CW_BULK_MODE");return p && p[0]=='1';}
    std::size_t pending() const noexcept {return initializations.size()+links.size();}
    void account() noexcept {
        if(!auditing)return;
        peak_pending=(std::max)(peak_pending,static_cast<std::uint64_t>(pending()));
        peak_bytes=(std::max)(peak_bytes,static_cast<std::uint64_t>(
            initializations.capacity()*sizeof(init)+links.capacity()*sizeof(link)+init_index.bytes()+link_index.bytes()));
    }
    server_status put_init(object_endpoint target,construction_value value,bool& replaced) noexcept {
        replaced=false;
        const auto k=access::key(target);
        if(!k)return server_status::project_configuration_invalid;
        if(pending()>=limit){const auto s=flush();if(!succeeded(s))return s;}
        const auto h=hash(k);
        if(const auto hit=init_index.find(k,h)){
            initializations[hit-1].value.value=value;replaced=true;if(auditing){++replacements;++init_calls;}
            return server_status::success;
        }
        const auto previous=access::find_init(G,target);
        if(previous && !access::valid_init(G,previous,target))return server_status::project_artifact_invalid;
        if(!previous && access::inits(G)+new_inits>=0xffffffffULL)return server_status::io_error;
        const auto final_slot=previous?previous:static_cast<std::uint32_t>(access::inits(G)+new_inits+1);
        try {
            initializations.push_back({{target,value},final_slot});
            init_index.insert(k,h,static_cast<std::uint32_t>(initializations.size()));
        } catch(...){failed=true;return server_status::io_error;}
        if(!previous)++new_inits;else{replaced=true;if(auditing)++replacements;}
        if(auditing)++init_calls;account();return server_status::success;
    }
public:
    const bool enabled;
    const bool auditing;
    explicit batch(graph& g) noexcept:G(g),enabled(selected()),auditing([]{const auto* p=std::getenv("CW_BULK_AUDIT");return p && p[0]=='1';}()) {
        if(const auto* p=std::getenv("CW_BULK_BLOCK")){
            const auto n=std::strtoull(p,nullptr,10);if(n && n<=1048576)limit=static_cast<std::size_t>(n);
        }
    }
    ~batch() noexcept {
        // Error exits still expose the successfully parsed prefix, as V2 does.
        if(enabled && !failed)(void)flush();
    }
    server_status flush() noexcept {
        if(failed)return server_status::io_error;
        if(!pending())return server_status::success;
        const auto status=access::prepare(G,new_inits,new_links);
        if(!succeeded(status)){failed=true;return status;}
        access::apply(G,initializations,links,new_inits,new_links);
        initializations.clear();links.clear();init_index.clear();link_index.clear();
        new_inits=new_links=0;if(auditing)++flushes;return server_status::success;
    }
    server_status finish() noexcept {
        const auto status=enabled?flush():server_status::success;
        if(const auto* p=std::getenv("CW_BULK_VERIFY");p && p[0]=='1')
            if(!access::verify(G))return server_status::project_artifact_invalid;
        if(auditing && !reported){
            std::printf("bulk_materialization01,mode=%d,block=%zu,init_calls=%llu,link_calls=%llu,flushes=%llu,replacements=%llu,peak_pending=%llu,buffer_bytes=%llu\n",
                enabled?1:0,limit,init_calls,link_calls,flushes,replacements,peak_pending,peak_bytes);reported=true;
        }
        return status;
    }
    server_status add_initialization(object_endpoint t,type_ref type,construction_value v,bool& replaced) noexcept {
        if(!enabled)return G.add_resolved_initialization(t,type,v,replaced);
        replaced=false;
        if(!access::scalar(G,type))return server_status::project_configuration_invalid;
        return put_init(t,v,replaced);
    }
    server_status add_initializations(type_ref type,std::span<const object_initialization_record> rr,bool& replaced) noexcept {
        if(!enabled)return G.add_resolved_initializations(type,rr,replaced);
        replaced=false;
        if(!access::scalar(G,type))return server_status::project_configuration_invalid;
        for(const auto& r:rr)if(!access::key(r.target))return server_status::project_configuration_invalid;
        for(const auto& r:rr){bool one=false;const auto s=put_init(r.target,r.value,one);
            if(!succeeded(s))return s;replaced|=one;}
        return server_status::success;
    }
    server_status rebind(object_endpoint source,object_endpoint target,link_handle& result,bool& replaced) noexcept {
        if(!enabled)return G.rebind_resolved_link(source,target,result,replaced);
        result={};replaced=false;
        if(!source.object || !source.member || !target.object || !target.member)
            return server_status::project_configuration_invalid;
        if(pending()>=limit){const auto s=flush();if(!succeeded(s))return s;}
        const auto k=access::key(target),h=hash(k);
        if(const auto hit=link_index.find(k,h)){
            auto& r=links[hit-1];r.value.source=source;result=access::handle(r.slot);
            replaced=true;if(auditing){++replacements;++link_calls;}return server_status::success;
        }
        const auto old=access::find_link(G,target);const auto previous=old.value();
        if(previous && !access::valid_link(G,previous,target))return server_status::project_artifact_invalid;
        if(!previous && access::links(G)+new_links>=link_handle::maximum_slot)return server_status::io_error;
        const auto final_slot=previous?previous:static_cast<std::uint32_t>(access::links(G)+new_links+1);
        try {links.push_back({{source,target},final_slot});link_index.insert(k,h,static_cast<std::uint32_t>(links.size()));}
        catch(...){failed=true;return server_status::io_error;}
        result=access::handle(final_slot);
        if(!previous)++new_links;else{replaced=true;if(auditing)++replacements;}
        if(auditing)++link_calls;account();return server_status::success;
    }
};
}
