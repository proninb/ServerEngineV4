#pragma once
// Used only by the allocation-diagnostic executable. Timing executable does
// not replace new/delete or enable request lifecycle atomics.
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

namespace sb02 {
enum stage : unsigned {off,input,pool_start,parse,combine,release_locals,canonical,binding,numbering,export_result,release_result,stage_count};
inline constexpr const char* stage_names[]={"off","input","pool_start","parse","combine","release_locals","canonical","binding","numbering","export_result","release_result"};
struct counter {
    std::atomic<std::uint64_t> calls=0,bytes=0,live=0,peak=0,live_allocations=0,peak_allocations=0,malloc_ns=0;
};
inline void maximum(std::atomic<std::uint64_t>& target,std::uint64_t n){auto previous=target.load(std::memory_order_relaxed);while(previous<n&&!target.compare_exchange_weak(previous,n,std::memory_order_relaxed)){} }
inline constexpr unsigned max_roots=16384;
inline std::array<counter,stage_count> stages;
inline std::array<counter,max_roots> roots;
inline counter total;
inline std::atomic<unsigned> active_stage=off;
inline thread_local unsigned active_root=max_roots;
inline std::atomic<std::uint64_t> live_requests=0,peak_requests=0;
inline std::array<std::atomic<std::uint64_t>,stage_count> phase_request_peaks{};
struct request_lifetime {
    request_lifetime()noexcept{const auto n=live_requests.fetch_add(1,std::memory_order_relaxed)+1;maximum(peak_requests,n);maximum(phase_request_peaks[active_stage.load(std::memory_order_relaxed)],n);}
    request_lifetime(const request_lifetime&)noexcept:request_lifetime(){}
    request_lifetime(request_lifetime&&)noexcept:request_lifetime(){}
    request_lifetime& operator=(const request_lifetime&)noexcept=default;
    request_lifetime& operator=(request_lifetime&&)noexcept=default;
    ~request_lifetime(){live_requests.fetch_sub(1,std::memory_order_relaxed);}
};
struct phase {
    unsigned previous;
    explicit phase(unsigned s):previous(active_stage.exchange(s,std::memory_order_relaxed)){
        maximum(phase_request_peaks[s],live_requests.load(std::memory_order_relaxed));
    }
    ~phase(){active_stage.store(previous,std::memory_order_relaxed);}
};
struct root_scope {
    unsigned previous;
    explicit root_scope(unsigned i):previous(active_root){active_root=i;}
    ~root_scope(){active_root=previous;}
};
inline void add(counter& c,std::uint64_t bytes,std::uint64_t ns){
    c.calls.fetch_add(1,std::memory_order_relaxed);c.bytes.fetch_add(bytes,std::memory_order_relaxed);c.malloc_ns.fetch_add(ns,std::memory_order_relaxed);
    maximum(c.peak,c.live.fetch_add(bytes,std::memory_order_relaxed)+bytes);
    maximum(c.peak_allocations,c.live_allocations.fetch_add(1,std::memory_order_relaxed)+1);
}
inline void remove(counter& c,std::uint64_t bytes){c.live.fetch_sub(bytes,std::memory_order_relaxed);c.live_allocations.fetch_sub(1,std::memory_order_relaxed);}

#ifdef SB02_ALLOCATIONS
struct allocation_header {void* base;std::size_t bytes;unsigned stage,root;};
inline void* allocate(std::size_t requested,std::size_t alignment){
    alignment=alignment<alignof(allocation_header)?alignof(allocation_header):alignment;
    const auto n=requested?requested:1;
    if(n>std::numeric_limits<std::size_t>::max()-sizeof(allocation_header)-alignment)throw std::bad_alloc();
    const auto s=active_stage.load(std::memory_order_relaxed);
    const auto start=s?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    void* base=std::malloc(n+sizeof(allocation_header)+alignment-1);
    if(!base)throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(base)+sizeof(allocation_header)+alignment-1)&~(alignment-1);
    auto* h=reinterpret_cast<allocation_header*>(address)-1;
    h->base=base;h->bytes=requested;h->stage=s;h->root=active_root;
    if(s){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        add(stages[s],requested,ns);add(total,requested,ns);if(h->root<max_roots)add(roots[h->root],requested,ns);}
    return reinterpret_cast<void*>(address);
}
inline void deallocate(void* p)noexcept{
    if(!p)return;auto* h=static_cast<allocation_header*>(p)-1;
    if(h->stage){remove(stages[h->stage],h->bytes);remove(total,h->bytes);if(h->root<max_roots)remove(roots[h->root],h->bytes);}
    std::free(h->base);
}
#endif
} // namespace sb02

#ifdef SB02_ALLOCATIONS
void* operator new(std::size_t n){return sb02::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return sb02::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t& t)noexcept{return ::operator new(n,t);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t& t)noexcept{return ::operator new(n,a,t);}
void operator delete(void* p)noexcept{sb02::deallocate(p);}
void operator delete[](void* p)noexcept{sb02::deallocate(p);}
void operator delete(void* p,std::size_t)noexcept{sb02::deallocate(p);}
void operator delete[](void* p,std::size_t)noexcept{sb02::deallocate(p);}
void operator delete(void* p,std::align_val_t)noexcept{sb02::deallocate(p);}
void operator delete[](void* p,std::align_val_t)noexcept{sb02::deallocate(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{sb02::deallocate(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{sb02::deallocate(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{sb02::deallocate(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{sb02::deallocate(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{sb02::deallocate(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{sb02::deallocate(p);}
#endif
