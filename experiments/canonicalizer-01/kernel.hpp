#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory_resource>
#include <mutex>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ca01 {
using id = std::uint32_t;
constexpr id none = UINT32_MAX;
constexpr std::uint64_t import_event = UINT64_MAX;
inline void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
inline std::uint64_t hash_text(std::string_view s) {
    std::uint64_t h=14695981039346656037ull;
    for(unsigned char c:s) {h^=c;h*=1099511628211ull;} return h;
}
inline std::uint64_t mix(std::uint64_t x) {x^=x>>30;x*=0xbf58476d1ce4e5b9ull;x^=x>>27;x*=0x94d049bb133111ebull;return x^(x>>31);}
struct symbol {
    id parent=none, worker=0, kind=0;
    std::string_view name;
    std::uint64_t event=import_event, hash=0;
    id expected=0; // Oracle only; never read by a canonicalization kernel.
};
struct query { id parent=0,kind=0,expected=0;std::string_view name;std::uint64_t event=0;bool search_parents=false; };
struct input {
    std::vector<symbol> symbols;
    std::vector<query> queries;
    std::vector<std::vector<id>> levels,children;
    bool strings=false, collisions=false;
    void prepare() {
        require(symbols.size()<none,"too many local symbols");
        levels.clear();children.assign(symbols.size(),{});
        std::vector<unsigned char> state(symbols.size()); std::vector<id> depth(symbols.size());
        std::function<id(id)> visit=[&](id i)->id {
            require(i<symbols.size(),"invalid local parent");
            require(state[i]!=1,"cyclic local parents");
            if(state[i]==2)return depth[i];
            state[i]=1;const auto& s=symbols[i];
            if(s.parent==none){require(s.kind==0 && s.name.empty(),"invalid root");depth[i]=0;}
            else {require(s.kind>=1 && s.kind<=3 && !s.name.empty(),"invalid symbol key");
                depth[i]=visit(s.parent)+1;require(depth[i]<=512,"scope depth limit");}
            state[i]=2;return depth[i];
        };
        for(id i=0;i<symbols.size();++i){const auto d=visit(i);if(levels.size()<=d)levels.resize(d+1);
            levels[d].push_back(i);if(symbols[i].parent!=none)children[symbols[i].parent].push_back(i);
        }
        require(!levels.empty()&&!levels[0].empty(),"missing root");
        for(const auto& q:queries)require(q.parent<symbols.size(),"invalid query parent");
    }
};
class memory final: public std::pmr::memory_resource {
public:
    std::atomic<std::size_t> live=0,peak=0;bool track;
    explicit memory(bool enabled):track(enabled){}
private:
    void* do_allocate(std::size_t n,std::size_t a) override {
#ifdef SB02_ALLOCATIONS
        // Diagnostic-only upstream: account for PMR requests too. MSVC's
        // new_delete_resource can bypass the replaceable global new overloads.
        auto* p=sb02::allocate(n,a);
#else
        auto* p=std::pmr::new_delete_resource()->allocate(n,a);
#endif
        if(track){auto v=live.fetch_add(n)+n;auto old=peak.load();while(old<v&&!peak.compare_exchange_weak(old,v)){} }
        return p;
    }
    void do_deallocate(void* p,std::size_t n,std::size_t a) override {
        if(track)live.fetch_sub(n);
#ifdef SB02_ALLOCATIONS
        (void)a;sb02::deallocate(p);
#else
        std::pmr::new_delete_resource()->deallocate(p,n,a);
#endif
    }
    bool do_is_equal(const std::pmr::memory_resource& r)const noexcept override{return this==&r;}
};
struct comparisons {std::uint64_t calls=0,span_bytes=0;};
class pool {
    std::vector<std::thread> threads;std::mutex mu;std::condition_variable ready,done;
    std::function<void(std::size_t,std::size_t)> job;
    std::atomic<std::size_t> next{0};std::size_t tasks=0,generation=0,finished=0;bool stop=false;
    std::exception_ptr error;
public:
    const std::size_t size;
    explicit pool(std::size_t n):size(n){require(n>=1&&n<=16,"workers must be 1..16");
        if(n==1)return;
        for(std::size_t w=0;w<n;++w)threads.emplace_back([&,w]{std::size_t seen=0;
            for(;;){std::unique_lock lock(mu);ready.wait(lock,[&]{return stop||generation!=seen;});
                if(stop)return;seen=generation;lock.unlock();
                try{for(auto i=next.fetch_add(1);i<tasks;i=next.fetch_add(1))job(i,w);}
                catch(...){std::lock_guard error_lock(mu);if(!error)error=std::current_exception();}
                lock.lock();if(++finished==size)done.notify_one();}
        });
    }
    ~pool(){ {std::lock_guard lock(mu);stop=true;} ready.notify_all();for(auto& t:threads)t.join(); }
    template<class F>void run(std::size_t count,F f){if(!count)return;
        if(size==1){for(std::size_t i=0;i<count;++i)f(i,0);return;}
        std::unique_lock lock(mu);job=f;tasks=count;next=0;finished=0;error=nullptr;++generation;
        ready.notify_all();done.wait(lock,[&]{return finished==size;});if(error)std::rethrow_exception(error);
        job={};
    }
};
struct key {id parent,kind;std::string_view name;std::uint64_t hash;};
inline int text_compare(std::string_view a,std::string_view b,comparisons* stats){
    const auto n=(std::min)(a.size(),b.size());
    if(stats){++stats->calls;stats->span_bytes+=n;}
    const int c=n?std::memcmp(a.data(),b.data(),n):0;
    if(c)return c;return a.size()<b.size()?-1:a.size()>b.size()?1:0;
}
inline int compare(key a,key b,comparisons* s){
    if(a.parent!=b.parent)return a.parent<b.parent?-1:1;
    if(a.kind!=b.kind)return a.kind<b.kind?-1:1;return text_compare(a.name,b.name,s);
}
inline std::size_t hash_key(key k,bool collisions){return collisions?0:static_cast<std::size_t>(mix(k.hash^mix(k.parent)^mix(k.kind)));}
struct hasher {bool collisions=false;std::size_t operator()(key k)const{
    // Partition bits must not also select buckets inside a power-of-two table.
    // Otherwise each shard can use only 1/shards of the MSVC table buckets.
    return collisions?0:static_cast<std::size_t>(mix(hash_key(k,false)^0x9e3779b97f4a7c15ull));
}};
struct equal {comparisons* stats=nullptr;bool operator()(key a,key b)const{return compare(a,b,stats)==0;}};
struct less {bool operator()(key a,key b)const{return compare(a,b,nullptr)<0;}};
struct result {
    std::pmr::vector<id> representative;
    std::pmr::vector<std::uint64_t> name_hashes;
    explicit result(std::pmr::memory_resource* r):representative(r),name_hashes(r){}
};
inline key make_key(const input& in,const result& out,id i){const auto& s=in.symbols[i];
    return {out.representative[s.parent],s.kind,s.name,out.name_hashes.empty()?0:out.name_hashes[i]};}
inline result reference(const input& in,std::pmr::memory_resource* r){
    result out(r);out.representative.assign(in.symbols.size(),none);
    std::pmr::map<key,id,less> classes{less{},r};
    std::function<id(id)> visit=[&](id i)->id{
        auto& rep=out.representative[i];if(rep!=none)return rep;
        const auto& s=in.symbols[i];if(s.parent==none)return rep=in.levels[0][0];
        const auto parent=visit(s.parent);key k{parent,s.kind,s.name,s.hash};
        const auto [it,inserted]=classes.emplace(k,i);(void)inserted;return rep=it->second;
    };
    // Reference labels can differ from kernels: only the partition and final WHO matter.
    for(id i=0;i<in.symbols.size();++i)visit(i);return out;
}
inline result canonicalize(const input& in,std::string_view algorithm,pool& workers,
    memory& mem,std::vector<comparisons>& stats,bool diagnostic){
    result out(&mem);out.representative.assign(in.symbols.size(),none);
    for(auto i:in.levels[0])out.representative[i]=in.levels[0][0];
    if(algorithm=="index"){
        // Raw-name input: charge name hashing to canonicalization, not input setup.
        out.name_hashes.resize(in.symbols.size());
        workers.run(workers.size,[&](auto c,auto){
            for(auto i=c*in.symbols.size()/workers.size;i<(c+1)*in.symbols.size()/workers.size;++i)
                out.name_hashes[i]=hash_text(in.symbols[i].name);
        });
    }
    auto counter=[&](std::size_t w)->comparisons*{return diagnostic?&stats[w]:nullptr;};
    for(std::size_t level=1;level<in.levels.size();++level){
        const auto& ids=in.levels[level];const auto n=ids.size();if(!n)continue;
        if(algorithm=="sort"){
            std::pmr::vector<id> a(ids.begin(),ids.end(),&mem),b(n,0,&mem);
            const auto chunks=(std::min)(workers.size,n);
            workers.run(chunks,[&](auto c,auto w){auto first=c*n/chunks,last=(c+1)*n/chunks;
                std::sort(a.begin()+first,a.begin()+last,[&](id x,id y){const auto cmp=compare(make_key(in,out,x),make_key(in,out,y),counter(w));return cmp<0||(cmp==0&&x<y);});});
            std::pmr::vector<std::pair<std::size_t,std::size_t>> runs(&mem);
            for(std::size_t c=0;c<chunks;++c)runs.emplace_back(c*n/chunks,(c+1)*n/chunks);
            while(runs.size()>1){const auto count=(runs.size()+1)/2;
                workers.run(count,[&](auto c,auto w){const auto start=runs[2*c].first;
                    if(2*c+1==runs.size()){std::copy(a.begin()+start,a.begin()+runs[2*c].second,b.begin()+start);return;}
                    const auto middle=runs[2*c].second,end=runs[2*c+1].second;
                    std::merge(a.begin()+start,a.begin()+middle,a.begin()+middle,a.begin()+end,b.begin()+start,
                        [&](id x,id y){const auto cmp=compare(make_key(in,out,x),make_key(in,out,y),counter(w));return cmp<0||(cmp==0&&x<y);});});
                std::pmr::vector<std::pair<std::size_t,std::size_t>> next(&mem);
                for(std::size_t c=0;c<count;++c)next.emplace_back(runs[2*c].first,runs[(std::min)(2*c+1,runs.size()-1)].second);
                runs.swap(next);a.swap(b);
            }
            // Group boundaries are numeric work after the parallel sort/merge.
            for(std::size_t first=0;first<n;){auto last=first+1;id rep=a[first];
                while(last<n&&compare(make_key(in,out,a[first]),make_key(in,out,a[last]),counter(0))==0){rep=(std::min)(rep,a[last]);++last;}
                for(auto p=first;p<last;++p)out.representative[a[p]]=rep;first=last;}
        }else if(algorithm=="index"){
            const std::size_t shards=workers.size*4;
            std::pmr::vector<std::size_t> counts(workers.size*shards,0,&mem),offsets(counts.size(),0,&mem);
            workers.run(workers.size,[&](auto c,auto){for(auto p=c*n/workers.size;p<(c+1)*n/workers.size;++p)
                ++counts[c*shards+hash_key(make_key(in,out,ids[p]),in.collisions)%shards];});
            std::pmr::vector<std::size_t> boundaries(shards+1,0,&mem);
            for(std::size_t s=0;s<shards;++s){auto pos=boundaries[s];for(std::size_t c=0;c<workers.size;++c){offsets[c*shards+s]=pos;pos+=counts[c*shards+s];}boundaries[s+1]=pos;}
            std::pmr::vector<id> partition(n,0,&mem);
            workers.run(workers.size,[&](auto c,auto){for(auto p=c*n/workers.size;p<(c+1)*n/workers.size;++p){auto i=ids[p];const auto s=hash_key(make_key(in,out,i),in.collisions)%shards;partition[offsets[c*shards+s]++]=i;}});
            workers.run(shards,[&](auto s,auto w){
                std::pmr::unordered_map<key,id,hasher,equal> table(0,hasher{in.collisions},equal{counter(w)},&mem);
                table.reserve(boundaries[s+1]-boundaries[s]);
                for(auto p=boundaries[s];p<boundaries[s+1];++p){auto i=partition[p];auto [it,added]=table.emplace(make_key(in,out,i),i);if(!added)it->second=(std::min)(it->second,i);}
                for(auto p=boundaries[s];p<boundaries[s+1];++p){auto i=partition[p];out.representative[i]=table.find(make_key(in,out,i))->second;}
            });
        }else if(algorithm=="hierarchical"){
            // Sorted child runs per LOCAL parent, merged only after parents agree.
            std::pmr::vector<id> parents(&mem);
            for(auto p:in.levels[level-1])if(!in.children[p].empty())parents.push_back(p);
            std::pmr::vector<id> offsets(in.symbols.size()+1,0,&mem),flat(n,0,&mem);
            id position=0;for(auto p:parents){offsets[p]=position;position+=static_cast<id>(in.children[p].size());offsets[p+1]=position;}
            workers.run(parents.size(),[&](auto task,auto w){auto p=parents[task],start=offsets[p];
                std::copy(in.children[p].begin(),in.children[p].end(),flat.begin()+start);
                std::sort(flat.begin()+start,flat.begin()+start+in.children[p].size(),[&](id x,id y){const auto cmp=compare(make_key(in,out,x),make_key(in,out,y),counter(w));return cmp<0||(cmp==0&&x<y);});});
            std::sort(parents.begin(),parents.end(),[&](id x,id y){return out.representative[x]<out.representative[y];});
            std::pmr::vector<std::pair<std::size_t,std::size_t>> groups(&mem);
            for(std::size_t i=0;i<parents.size();){auto j=i+1;while(j<parents.size()&&out.representative[parents[i]]==out.representative[parents[j]])++j;groups.emplace_back(i,j);i=j;}
            workers.run(groups.size(),[&](auto task,auto w){
                struct cursor{std::size_t at,end;};
                auto greater=[&](cursor x,cursor y){const auto a=flat[x.at],b=flat[y.at];const auto cmp=compare(make_key(in,out,a),make_key(in,out,b),counter(w));return cmp>0||(cmp==0&&a>b);};
                std::pmr::vector<cursor> storage(&mem);
                std::priority_queue<cursor,std::pmr::vector<cursor>,decltype(greater)> heap(greater,std::move(storage));
                for(auto t=groups[task].first;t<groups[task].second;++t){auto p=parents[t];heap.push({offsets[p],offsets[p]+in.children[p].size()});}
                std::pmr::vector<id> equal_ids(&mem);
                auto finish=[&]{if(equal_ids.empty())return;auto rep=*std::min_element(equal_ids.begin(),equal_ids.end());for(auto i:equal_ids)out.representative[i]=rep;equal_ids.clear();};
                while(!heap.empty()){auto c=heap.top();heap.pop();auto i=flat[c.at];
                    if(!equal_ids.empty()&&compare(make_key(in,out,equal_ids.front()),make_key(in,out,i),counter(w))!=0)finish();
                    equal_ids.push_back(i);if(++c.at<c.end)heap.push(c);}
                finish();
            });
        }else throw std::runtime_error("unknown algorithm");
    }
    std::pmr::vector<std::uint64_t> released(&mem);out.name_hashes.swap(released);
    return out;
}
struct numbering {
    std::pmr::vector<id> who;
    std::pmr::vector<std::uint64_t> first;
    std::size_t classes=0,cross_worker_duplicates=0;
    explicit numbering(std::pmr::memory_resource* r):who(r),first(r){}
};
inline numbering number(const input& in,const result& classes,memory& mem){
    numbering out(&mem);const auto n=in.symbols.size();out.who.assign(n,0);out.first.assign(n,import_event);
    std::pmr::vector<id> order(&mem);
    for(id i=0;i<n;++i)if(in.symbols[i].parent!=none){auto rep=classes.representative[i];require(rep<n,"unresolved representative");out.first[rep]=(std::min)(out.first[rep],in.symbols[i].event);}
    for(auto root:in.levels[0])out.first[classes.representative[root]]=0;
    for(id i=0;i<n;++i)if(in.symbols[i].parent!=none)
        require(out.first[classes.representative[in.symbols[i].parent]]<out.first[classes.representative[i]],"parent created after child");
    for(id i=0;i<n;++i)if(classes.representative[i]==i&&in.symbols[i].parent!=none){require(out.first[i]!=import_event,"import without declaration");order.push_back(i);}
    std::sort(order.begin(),order.end(),[&](id a,id b){return out.first[a]<out.first[b];});
    for(std::size_t i=1;i<order.size();++i)require(out.first[order[i-1]]!=out.first[order[i]],"ambiguous creation event order");
    id slot=in.strings?1:2;
    for(auto i:order){require(slot<0x40000000u,"WHO slot limit");out.who[i]=in.strings?slot:(in.symbols[i].kind<<30)|slot;++slot;}
    for(auto root:in.levels[0])out.who[root]=in.strings?0:1;
    for(id i=0;i<n;++i)out.who[i]=out.who[classes.representative[i]];
    out.classes=order.size()+(in.strings?0:1);
    return out;
}
inline void verify(const input& in,const result& classes,const numbering& numbered){
    std::unordered_map<key,id,hasher,equal> index;
    for(id i=0;i<in.symbols.size();++i){const auto& s=in.symbols[i];require(numbered.who[i]==s.expected,"WHO/string_id parity mismatch");
        if(s.parent!=none){auto k=make_key(in,classes,i);k.hash=hash_text(k.name);index.emplace(k,classes.representative[i]);}}
    for(const auto& q:in.queries){id actual=0;
        for(id scope=q.parent;scope!=none;scope=in.symbols[scope].parent){
            key k{classes.representative[scope],q.kind,q.name,hash_text(q.name)};
            auto it=index.find(k);
            if(numbered.first[classes.representative[scope]]<=q.event&&it!=index.end()&&numbered.first[it->second]<=q.event){actual=numbered.who[it->second];break;}
            if(!q.search_parents)break;
        }
        require(actual==q.expected,"binding visibility mismatch");}
}
} // namespace ca01
