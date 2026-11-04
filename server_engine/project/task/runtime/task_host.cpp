#include "task_host.hpp"
#include "task_protocol.hpp"
#include "../../../fixed_shared_memory.hpp"
#include <cstring>
#include <limits>
#include <Windows.h>
namespace cw::server::task_runtime {
namespace {
cw::server::fixed_shared_memory attached_mapping;
bool read_bytes(HANDLE h, void* data, unsigned count) {
    char* p=static_cast<char*>(data);
    while(count) { DWORD got=0; if(!ReadFile(h,p,count,&got,nullptr)||!got) return false; p+=got; count-=got; }
    return true;
}
bool read_exact(HANDLE h,message& m) {
    char* p=reinterpret_cast<char*>(&m); unsigned remaining=sizeof(m);
    while(remaining) { DWORD got=0; if(!ReadFile(h,p,remaining,&got,nullptr)||!got) return false; p+=got; remaining-=got; }
    return true;
}
bool send(HANDLE h,command action,std::uint64_t cycle=0,std::uint32_t status=0) {
    message reply{magic,version,action,0,cycle,0,status}; DWORD n=0;
    return WriteFile(h,&reply,sizeof(reply),&n,nullptr)&&n==sizeof(reply);
}
}
void* shared_memory_data() noexcept { return attached_mapping.data(); }
std::size_t shared_memory_size() noexcept { return attached_mapping.size(); }
int run_host(callbacks cb) {
    if(!cb.init||!cb.main||!cb.done) return 2;
    HANDLE in=GetStdHandle(STD_INPUT_HANDLE),out=GetStdHandle(STD_OUTPUT_HANDLE);
    if(in==INVALID_HANDLE_VALUE||out==INVALID_HANDLE_VALUE) return 3;
    message request{};
    if(!read_exact(in,request)||request.signature!=magic||request.protocol_version!=version) return 4;
    if(request.action==command::attach) {
        shm_attachment attachment{};
        if(!read_bytes(in,&attachment,sizeof(attachment)) ||
           attachment.name[0]=='\0' || attachment.name[128]!='\0' ||
           std::memchr(attachment.name,'\0',sizeof(attachment.name))==nullptr ||
           attachment.size>static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) ||
           attachment.address>static_cast<std::uint64_t>((std::numeric_limits<std::uintptr_t>::max)()) ||
           attached_mapping.open(attachment.name,static_cast<std::size_t>(attachment.size),
               static_cast<std::uintptr_t>(attachment.address))!=cw::server::fixed_shared_memory_result::success) {
            send(out,command::error,0,12);
            return 12;
        }
        if(!send(out,command::attached)) return 13;
        if(!read_exact(in,request)||request.signature!=magic||request.protocol_version!=version) return 4;
    }
    if(request.action!=command::init) return 4;
    bool initialized=false;
    try { initialized=cb.init(); } catch(...) { initialized=false; }
    if(!initialized) { send(out,command::error,0,1); return 5; }
    if(!send(out,command::ready)) return 6;
    while(read_exact(in,request)) {
        if(request.signature!=magic||request.protocol_version!=version) break;
        if(request.action==command::stop) {
            try { cb.done(); } catch(...) { send(out,command::error,0,3); return 7; }
            return send(out,command::stopped)?0:8;
        }
        if(request.action!=command::run||!request.calls) break;
        bool ok=true;
        try { for(std::uint32_t i=0;i<request.calls;++i) if(!cb.main()) { ok=false; break; } } catch(...) { ok=false; }
        if(!ok) { send(out,command::error,request.cycle,2); return 9; }
        if(!send(out,command::done,request.cycle)) return 10;
    }
    return 11; // Abnormal control-channel closure: no graceful TaskDone.
}
}
