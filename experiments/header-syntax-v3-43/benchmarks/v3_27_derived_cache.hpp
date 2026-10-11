#pragma once
// V3-27: producer-owned canonical derived-type cache. Exact key includes
// child type_ref (with kind), derived kind, and full 64-bit array payload.
// The cache is never shared between Graph instances or PUBLISH generations.
// Graph remains the only authority for creating a new canonical WHERE.
#include <cstdint>
#include <cstddef>
#include <vector>
#include <limits>
#include <new>
#include <type_traits>

namespace v327_derived {
using namespace cw::server;
struct counters final {
    std::uint64_t calls=0, hits=0, misses=0, graph_created=0;
};
class cache final {
    struct slot final {
        std::uint64_t payload=0;
        std::uint32_t child=0;
        type_ref result{};
        derived_type_kind kind=derived_type_kind::pointer;
    };
    static_assert(std::is_trivially_copyable_v<slot>);
    std::vector<slot> table_;
    std::size_t entries_=0;
    [[nodiscard]] static std::uint64_t mix(std::uint64_t x) noexcept {
        x^=x>>30;x*=0xbf58476d1ce4e5b9ull;
        x^=x>>27;x*=0x94d049bb133111ebull;
        return x^(x>>31);
    }
    // Hash packed 32-bit WHO/WHERE reference without constructing type_ref.
    [[nodiscard]] static std::uint64_t hash_key(std::uint32_t child,
        derived_type_kind kind,std::uint64_t payload) noexcept {
        const auto key=(static_cast<std::uint64_t>(child)<<8)|
            static_cast<std::uint8_t>(kind);
        return mix(key^mix(payload+0x9e3779b97f4a7c15ull));
    }
    void grow() {
        constexpr std::size_t maximum_slots=1u<<21;
        const auto count=table_.empty()?1024u:table_.size()*2;
        if(count>maximum_slots)return;
        std::vector<slot> next(count);
        for(const auto& s:table_)if(s.result){
            const auto mask=next.size()-1;
            auto p=static_cast<std::size_t>(hash_key(s.child,s.kind,s.payload))&mask;
            while(next[p].result)p=(p+1)&mask;
            next[p]=s;
        }
        table_.swap(next);
    }
public:
    [[nodiscard]] bool find(type_ref child,derived_type_kind kind,
        std::uint64_t payload,type_ref& result) const noexcept {
        result={};
        if(table_.empty())return false;
        const auto mask=table_.size()-1;
        auto p=static_cast<std::size_t>(hash_key(child.value(),kind,payload))&mask;
        for(std::size_t count=0;count<table_.size();++count){
            const auto& s=table_[p];
            if(!s.result)return false;
            if(s.child==child.value() && s.kind==kind && s.payload==payload){
                result=s.result;return true;
            }
            p=(p+1)&mask;
        }
        return false;
    }
    // Graph is called only on misses. Failed calls are never cached.
    [[nodiscard]] server_status intern(graph& G,type_ref child,
        derived_type_kind kind,std::uint64_t payload,type_ref& output,
        counters* stats=nullptr) {
        output={};
        if(stats)++stats->calls;
        if(find(child,kind,payload,output)){
            if(stats)++stats->hits;
            return server_status::success;
        }
        if(stats)++stats->misses;
        const auto before=G.derived_type_count();
        const auto result=G.derive_resolved(child,kind,payload,output);
        if(!succeeded(result)||!output)return result;
        if(stats && G.derived_type_count()>before)
            stats->graph_created+=G.derived_type_count()-before;
        // Scratch caching is best effort. Graph has already committed the
        // canonical result; a cache allocation failure must not change it.
        try {
            if(table_.empty() || entries_+1>table_.size()/2)grow();
            if(!table_.empty() && entries_+1<=table_.size()/2){
                const auto mask=table_.size()-1;
                auto p=static_cast<std::size_t>(hash_key(child.value(),kind,payload))&mask;
                while(table_[p].result)p=(p+1)&mask;
                table_[p]={payload,child.value(),output,kind};
                ++entries_;
            }
        } catch (const std::bad_alloc&) {
            // Continue with canonical Graph result, skipping this cache entry.
        }
        return result;
    }
    [[nodiscard]] std::size_t size()const noexcept {return entries_;}
};
} // namespace v327_derived
