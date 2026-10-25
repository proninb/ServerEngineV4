#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <fstream>
#include <cstdlib>
#include <new>

namespace pls04 {
using clock=std::chrono::steady_clock;
enum phase:unsigned {off,extraction,linking,local_release,aliases,consumer,ir_release,phase_count};
inline constexpr const char* names[]={"off","extraction","linking","local_release","aliases","consumer","ir_release"};
inline thread_local phase active=off;
struct heap_stats {std::uint64_t calls=0,bytes=0,live=0,peak=0,frees=0;};
inline std::array<heap_stats,phase_count> heap;
inline std::uint64_t total_live=0,total_peak=0;
inline double file_destruction_ms=0,tail_destruction_ms=0;
inline std::uint64_t released_files=0,released_capacity=0;
inline bool early(){static const bool value=[] {const char* p=std::getenv("PLS04_EARLY");return p&&p[0]=='1';}();return value;}
inline std::atomic<bool> stop{false};
inline std::atomic<unsigned> rss_phase{off};
inline std::array<std::atomic<std::uint64_t>,phase_count> rss{},private_bytes{};
inline std::atomic<std::uint64_t> samples{0};
inline std::thread sampler;
inline PROCESS_MEMORY_COUNTERS_EX initial{},last{};
inline void maximum(std::atomic<std::uint64_t>& value,std::uint64_t n){auto old=value.load();while(old<n&&!value.compare_exchange_weak(old,n)){};}
inline void sample_memory(){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))std::abort();
    const auto p=rss_phase.load();if(p!=off){maximum(rss[p],m.WorkingSetSize);maximum(private_bytes[p],m.PrivateUsage);++samples;}}
inline void phase_begin(phase p){active=p;rss_phase=p;sample_memory();}
inline void start(){
    initial.cb=sizeof(initial);if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&initial),sizeof(initial)))std::abort();
    sampler=std::thread([] {while(!stop.load()){sample_memory();std::this_thread::sleep_for(std::chrono::milliseconds(2));}});
    phase_begin(extraction);
}
inline void pause(){phase_begin(off);}
inline void finish(){phase_begin(off);stop=true;if(sampler.joinable())sampler.join();last.cb=sizeof(last);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&last),sizeof(last)))std::abort();}
// Join even when a negative fixture throws before the normal finish boundary.
struct guard {~guard(){if(sampler.joinable())finish();}};
template<class V> void discard(V& v){V{}.swap(v);}
template<class Model> void release_file(Model& m){if(!early())return;
    const auto previous=active;active=local_release;const auto start=clock::now();const auto bytes=m.retained_bytes();
    // model{} allocates its initial scope: release columns without constructing
    // another parser state. Keep only inert scalar metadata until vector teardown.
    discard(m.text);discard(m.names);discard(m.components);discard(m.scopes);discard(m.symbols);
    discard(m.selectors);discard(m.endpoints);discard(m.types);discard(m.extents);
    discard(m.bytes);discard(m.values);discard(m.events);discard(m.error);
    file_destruction_ms+=std::chrono::duration<double,std::milli>(clock::now()-start).count();++released_files;released_capacity+=bytes;
    active=previous;
}
inline void write(const char* path){if(!path)return;std::ofstream f(path);
    f<<"{\"early\":"<<(early()?"true":"false")<<",\"file_destruction_ms\":"<<file_destruction_ms
     <<",\"tail_destruction_ms\":"<<tail_destruction_ms<<",\"released_files\":"<<released_files<<",\"released_capacity\":"<<released_capacity
     <<",\"os_process_peak_before\":"<<initial.PeakWorkingSetSize<<",\"os_process_peak_after\":"<<last.PeakWorkingSetSize
     <<",\"samples\":"<<samples.load()<<",\"heap_peak_live_requested\":"<<total_peak<<",\"heap_live_requested_end\":"<<total_live<<",\"phases\":{";
    for(unsigned p=1;p<phase_count;++p){if(p>1)f<<',';const auto& h=heap[p];f<<'"'<<names[p]<<"\":{\"sampled_peak_working_set\":"<<rss[p].load()
      <<",\"sampled_peak_private_bytes\":"<<private_bytes[p].load()<<",\"allocations\":"<<h.calls<<",\"requested_bytes\":"<<h.bytes
      <<",\"live_requested\":"<<h.live<<",\"peak_live_requested\":"<<h.peak<<",\"frees\":"<<h.frees<<'}';}f<<"}}\n";
}
}

#ifdef PLS04_ALLOCATIONS
namespace pls04 {
struct header {void* base;std::size_t bytes;phase owner;};
inline void* allocate(std::size_t requested,std::size_t alignment){alignment=(std::max)(alignment,alignof(header));const auto n=requested?requested:1;
    if(n>static_cast<std::size_t>(-1)-sizeof(header)-alignment)throw std::bad_alloc();
    auto* raw=std::malloc(n+sizeof(header)+alignment-1);if(!raw)throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(header)+alignment-1)&~(alignment-1);
    auto* h=reinterpret_cast<header*>(address)-1;h->base=raw;h->bytes=requested;h->owner=active;
    if(active!=off){auto& s=heap[active];++s.calls;s.bytes+=requested;s.live+=requested;s.peak=(std::max)(s.peak,s.live);
      total_live+=requested;total_peak=(std::max)(total_peak,total_live);}return reinterpret_cast<void*>(address);}
inline void deallocate(void* p)noexcept{if(!p)return;auto* h=static_cast<header*>(p)-1;if(h->owner!=off){auto& s=heap[h->owner];++s.frees;s.live-=h->bytes;total_live-=h->bytes;}std::free(h->base);}
}
void* operator new(std::size_t n){return pls04::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return pls04::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t& t)noexcept{return ::operator new(n,t);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t& t)noexcept{return ::operator new(n,a,t);}
void operator delete(void* p)noexcept{pls04::deallocate(p);}
void operator delete[](void* p)noexcept{pls04::deallocate(p);}
void operator delete(void* p,std::size_t)noexcept{pls04::deallocate(p);}
void operator delete[](void* p,std::size_t)noexcept{pls04::deallocate(p);}
void operator delete(void* p,std::align_val_t)noexcept{pls04::deallocate(p);}
void operator delete[](void* p,std::align_val_t)noexcept{pls04::deallocate(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{pls04::deallocate(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{pls04::deallocate(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{pls04::deallocate(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{pls04::deallocate(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{pls04::deallocate(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{pls04::deallocate(p);}
#endif
