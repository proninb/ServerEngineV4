#include "audit.hpp"
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <new>
#include <algorithm>
namespace {
struct header{void* base;std::size_t bytes;bool counted;};
void* allocate(std::size_t n,std::size_t alignment){
 alignment=(std::max)(alignment,alignof(header));
 if(n>std::size_t(-1)-sizeof(header)-alignment)throw std::bad_alloc();
 auto raw=std::malloc((n?n:1)+sizeof(header)+alignment-1);if(!raw)throw std::bad_alloc();
 auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(header)+alignment-1)&~(alignment-1);
 auto h=reinterpret_cast<header*>(address)-1;*h={raw,n,pls10c9audit::active};
 if(h->counted){using namespace pls10c9audit;++allocations;requested+=n;++group_calls[group];group_bytes[group]+=n;auto v=live.fetch_add(n)+n;auto old=peak.load();while(v>old&&!peak.compare_exchange_weak(old,v)){};}
 return reinterpret_cast<void*>(address);
}
void release(void* p){if(!p)return;auto h=static_cast<header*>(p)-1;if(h->counted)pls10c9audit::live-=h->bytes;std::free(h->base);}
}
void* operator new(std::size_t n){return allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t& a)noexcept{return ::operator new(n,a);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t& b)noexcept{return ::operator new(n,a,b);}
void operator delete(void* p)noexcept{release(p);}
void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t)noexcept{release(p);}
void operator delete(void* p,std::align_val_t)noexcept{release(p);}
void operator delete[](void* p,std::align_val_t)noexcept{release(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{release(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{release(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{release(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{release(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{release(p);}
