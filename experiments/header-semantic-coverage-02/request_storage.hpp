#pragma once
#include <memory>
#include <iterator>

namespace sb01 {
// Shared ownership is batch-level, never per request. The store becomes
// immutable before Binding. Segment vector buffers retain stable addresses.
class request_storage {
public:
    struct arena {
        std::vector<std::shared_ptr<const shape_arena>> shape_owners;
        std::vector<request> flat;
        std::vector<std::vector<request>> segments;
        std::vector<std::size_t> starts;
        std::size_t count=0;
        bool segmented=false,frozen=false;
        const request& at(std::size_t i)const {
            ca01::require(i<count,"request index outside arena");
            if(!segmented)return flat[i];
            const auto segment=static_cast<std::size_t>(std::upper_bound(starts.begin(),starts.end(),i)-starts.begin()-1);
            return segments[segment][i-starts[segment]];
        }
    };
private:
    std::shared_ptr<arena> data;
public:
    request_storage()=default;
    void prepare(bool segmented,std::size_t count,std::size_t nonempty){
        data=std::make_shared<arena>();data->segmented=segmented;
        if(segmented){data->segments.reserve(nonempty);data->starts.reserve(nonempty);}
        else data->flat.reserve(count);
    }
    void retain(std::shared_ptr<const shape_arena> owner){if(!owner->payload.empty())data->shape_owners.push_back(std::move(owner));}
    void append(std::vector<request>& local){
        ca01::require(data&&!data->frozen,"cannot mutate frozen request storage");
        if(data->segmented){if(local.empty())return;data->starts.push_back(data->count);data->count+=local.size();data->segments.push_back(std::move(local));}
        else {for(auto& r:local)data->flat.push_back(std::move(r));data->count=data->flat.size();}
    }
    void freeze(){ca01::require(bool(data),"missing request arena");data->frozen=true;}
    std::shared_ptr<const arena> owner()const {ca01::require(data&&data->frozen,"request arena is not frozen");return data;}
    std::size_t size()const{return data?data->count:0;}
    std::size_t capacity()const{if(!data)return 0;if(!data->segmented)return data->flat.capacity();std::size_t n=0;for(const auto& s:data->segments)n+=s.capacity();return n;}
    std::size_t metadata_bytes()const{return data?sizeof(arena)+data->segments.capacity()*sizeof(std::vector<request>)+data->starts.capacity()*sizeof(std::size_t):0;}
    const request& operator[](std::size_t i)const{return data->at(i);}
    template<class F>void range(std::size_t first,std::size_t last,F&& f)const {
        ca01::require(first<=last&&last<=size(),"invalid request range");if(first==last)return;
        if(!data->segmented){for(auto i=first;i<last;++i)f(i,data->flat[i]);return;}
        auto s=static_cast<std::size_t>(std::upper_bound(data->starts.begin(),data->starts.end(),first)-data->starts.begin()-1);
        for(auto i=first;i<last;++s){const auto end=(std::min)(last,data->starts[s]+data->segments[s].size());
            for(;i<end;++i)f(i,data->segments[s][i-data->starts[s]]);}
    }
    // Sequential iteration traverses segments directly; no per-record search.
    struct iterator {
        using iterator_category=std::forward_iterator_tag;
        using value_type=request;using difference_type=std::ptrdiff_t;using pointer=const request*;using reference=const request&;
        const arena* a=nullptr;std::size_t index=0,segment=0,offset=0;
        reference operator*()const{return a->segmented?a->segments[segment][offset]:a->flat[index];}
        pointer operator->()const{return &**this;}
        iterator& operator++(){++index;if(a->segmented&&++offset==a->segments[segment].size()){++segment;offset=0;}return *this;}
        iterator operator++(int){auto previous=*this;++*this;return previous;}
        friend bool operator==(const iterator& x,const iterator& y){return x.a==y.a&&x.index==y.index;}
    };
    iterator begin()const{return {data.get(),0,0,0};}
    iterator end()const{return {data.get(),size(),0,0};}
};
}
