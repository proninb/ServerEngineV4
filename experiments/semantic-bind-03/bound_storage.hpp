#pragma once
namespace sb01 {
struct bound_type {id entity=none;I builtin=I::none;std::vector<modifier> layers;friend bool operator==(const bound_type&,const bound_type&)=default;};
struct resolved_leaf {id entity=none;I builtin=I::none;};
static_assert(sizeof(resolved_leaf)==8);
struct bound_view {id entity;I builtin;std::span<const modifier> layers;};
inline bool equal(bound_view a,bound_view b){return a.entity==b.entity&&a.builtin==b.builtin&&std::equal(a.layers.begin(),a.layers.end(),b.layers.begin(),b.layers.end());}
inline bool operator==(const bound_type& a,bound_view b){return equal({a.entity,a.builtin,a.layers},b);}
class bound_storage {
    std::vector<bound_type> owned;
    std::vector<resolved_leaf> leaves;
    std::shared_ptr<const request_storage::arena> shapes;
public:
    template<bool Shared>void initialize(std::size_t n,const request_storage& requests){
        if constexpr(Shared){shapes=requests.owner();leaves.resize(n);}else owned.resize(n);
    }
    template<bool Shared>auto& writable(std::size_t i){if constexpr(Shared)return leaves[i];else return owned[i];}
    bound_view operator[](std::size_t i)const{
        if(shapes){const auto& t=leaves[i];return {t.entity,t.builtin,shapes->at(i).type.layers};}
        const auto& t=owned[i];return {t.entity,t.builtin,t.layers};
    }
    std::size_t size()const{return shapes?leaves.size():owned.size();}
    std::size_t record_bytes()const{return leaves.capacity()*sizeof(resolved_leaf)+owned.capacity()*sizeof(bound_type);}
    std::size_t copied_modifier_bytes()const{std::size_t bytes=0;for(const auto& t:owned)bytes+=t.layers.capacity()*sizeof(modifier);return bytes;}
    // Exposes no borrowed shape without also retaining the whole owner in this
    // object. A bound_view itself has ordinary span borrowing rules.
    bool shares_shapes()const{return bool(shapes);}
    friend bool operator==(const bound_storage& a,const bound_storage& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!equal(a[i],b[i]))return false;return true;}
};
}
