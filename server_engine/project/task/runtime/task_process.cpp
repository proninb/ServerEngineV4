#include "task_process.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
namespace cw::server::task_runtime {
namespace {
void close_handle(HANDLE& h) noexcept { if(h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h=nullptr; }
bool write_exact(HANDLE h, const message& m) {
    DWORD written=0;
    return WriteFile(h,&m,sizeof(m),&written,nullptr) && written==sizeof(m);
}
}
process::~process(){ terminate(); }
void process::terminate() noexcept {
    if(child) { if(WaitForSingleObject(child,0)==WAIT_TIMEOUT) TerminateProcess(child,1); WaitForSingleObject(child,INFINITE); }
    close_handle(input); close_handle(output); close_handle(thread); close_handle(child);
    initialized=false;
}
bool process::start(const std::wstring& executable, std::uint32_t timeout_ms, std::string& error) {
    return start_impl(executable,timeout_ms,nullptr,error);
}
bool process::start(const std::wstring& executable, std::uint32_t timeout_ms,
    const shm_attachment& attachment, std::string& error) {
    return start_impl(executable,timeout_ms,&attachment,error);
}
bool process::start_impl(const std::wstring& executable, std::uint32_t timeout_ms,
    const shm_attachment* attachment, std::string& error) {
    terminate(); timeout=timeout_ms;
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE child_read=nullptr, child_write=nullptr;
    if(!CreatePipe(&child_read,&input,&sa,0)) { error="CreatePipe input failed"; return false; }
    if(!SetHandleInformation(input,HANDLE_FLAG_INHERIT,0) || !CreatePipe(&output,&child_write,&sa,0) || !SetHandleInformation(output,HANDLE_FLAG_INHERIT,0)) {
        error="CreatePipe output failed"; close_handle(child_read); close_handle(child_write); terminate(); return false;
    }
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=child_read; startup.hStdOutput=child_write; startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION info{};
    std::wstring cmd=L"\""+executable+L"\" --serverengine-task-host";
    std::vector<wchar_t> args(cmd.begin(),cmd.end()); args.push_back(L'\0');
    const BOOL ok=CreateProcessW(executable.c_str(),args.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info);
    close_handle(child_read); close_handle(child_write);
    if(!ok) { error="CreateProcessW failed: "+std::to_string(GetLastError()); terminate(); return false; }
    child=info.hProcess; thread=info.hThread;
    if(attachment) {
        if(!attachment->name[0] || attachment->name[128]!='\0' ||
           !std::memchr(attachment->name,'\0',sizeof(attachment->name)) ||
           !attachment->size || !attachment->address) {
            error="Invalid Task SHM attachment"; terminate(); return false;
        }
        const message request{magic,version,command::attach};
        DWORD written=0;
        if(!write_exact(input,request) ||
           !WriteFile(input,attachment,sizeof(*attachment),&written,nullptr) ||
           written!=sizeof(*attachment) ||
           !receive(command::attached,0,error)) { terminate(); return false; }
    }
    if(!exchange(message{magic,version,command::init},command::ready,error)) { terminate(); return false; }
    initialized=true; return true;
}
bool process::exchange(message request,command expected,std::string& error) {
    if(!child || !write_exact(input,request)) { error="Task control write failed"; return false; }
    return receive(expected,request.cycle,error);
}
bool process::receive(command expected,std::uint64_t cycle,std::string& error) {
    // Pipes are blocking: poll availability with bounded wall-clock timeout, and process liveness.
    const ULONGLONG deadline=GetTickCount64()+timeout;
    message response{}; std::size_t received=0;
    while(received<sizeof(response)) {
        DWORD available=0;
        if(!PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)) { error="Task control pipe closed"; return false; }
        if(available) {
            DWORD got=0;
            const DWORD want=static_cast<DWORD>((std::min)(static_cast<std::size_t>(available),sizeof(response)-received));
            if(!ReadFile(output,reinterpret_cast<char*>(&response)+received,want,&got,nullptr) || !got) { error="Task control read failed"; return false; }
            received+=got; continue;
        }
        if(WaitForSingleObject(child,0)!=WAIT_TIMEOUT) { error="Task exited before reply"; return false; }
        if(GetTickCount64()>=deadline) { error="Task reply timeout"; return false; }
        Sleep(1);
    }
    if(response.signature!=magic || response.protocol_version!=version || response.action!=expected || response.cycle!=cycle || response.status!=0) {
        error="Invalid Task reply, cycle or status"; return false;
    }
    return true;
}
bool process::run(std::uint64_t cycle,std::uint32_t calls,std::string& error) {
    if(!initialized || !calls) { error="Task not initialized or calls zero"; return false; }
    if(!exchange(message{magic,version,command::run,0,cycle,calls},command::done,error)) { terminate(); return false; }
    return true;
}
bool process::stop(std::string& error) {
    if(!initialized) { error="Task not initialized"; return false; }
    const bool ok=exchange(message{magic,version,command::stop},command::stopped,error);
    if(ok && WaitForSingleObject(child,timeout)!=WAIT_OBJECT_0) { error="Task did not exit after STOP"; terminate(); return false; }
    terminate(); return ok;
}
}
