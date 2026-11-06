#include "task_process.hpp"
#include "task_control.hpp"
#include "task_trace.hpp"
#include <Windows.h>
#include <atomic>
#include <string>
#include <new>
#include <vector>
namespace cw::server::task_runtime {
namespace {
void close_handle(HANDLE& h) noexcept { if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); h=nullptr; }
std::wstring unique_name() {
    static std::atomic<unsigned long> counter{0};
    return L"Local\\SEV4_Task_"+std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64())+L"_"+std::to_wstring(++counter);
}
}
process::~process() { terminate(); }
void process::terminate() noexcept {
    if(child) {
        if(WaitForSingleObject(child,0)==WAIT_TIMEOUT) TerminateProcess(child,1);
        WaitForSingleObject(child,INFINITE);
    }
    if(control) { UnmapViewOfFile(control); control=nullptr; }
    close_handle(command_event); close_handle(completion_event);
    close_handle(mapping); close_handle(thread); close_handle(child);
    perf_trace.close();
    if (dispatch_file) { std::fclose(dispatch_file); dispatch_file=nullptr; }
    initialized=false; sequence=0; in_flight=false; in_flight_sequence=0; in_flight_cycle=0; in_flight_start=0;
}
bool process::start(const std::wstring& executable,std::uint32_t timeout_ms,std::string& error) {
    return start_impl(executable,timeout_ms,nullptr,error);
}
bool process::start(const std::wstring& executable,std::uint32_t timeout_ms,
                    const shm_attachment& attachment,std::string& error) {
    return start_impl(executable,timeout_ms,&attachment,error);
}
bool process::start_impl(const std::wstring& executable,std::uint32_t timeout_ms,
                         const shm_attachment* attachment,std::string& error) {
    terminate(); timeout=timeout_ms;
    const auto name=unique_name();
    perf_trace.open(name,L"parent");
    // PERF-49C: opt-in, buffered dispatch timestamps; no SHM ABI changes.
    wchar_t diagnostic_directory[32768]{};
    const DWORD diagnostic_length=GetEnvironmentVariableW(L"TASK_SHM_PERF07_DIR",diagnostic_directory,32768);
    if (diagnostic_length && diagnostic_length < 32768) {
        std::wstring safe=name;
        for (auto& c:safe) if (c==L'\\' || c==L'/' || c==L':' || c==L' ') c=L'_';
        const auto path=std::wstring(diagnostic_directory)+L"\\dispatch_"+safe+L".csv";
        if (_wfopen_s(&dispatch_file,path.c_str(),L"wb")==0 && dispatch_file) {
            setvbuf(dispatch_file,nullptr,_IOFBF,1024*1024);
            std::fprintf(dispatch_file,"seq,cycle,before_setevent_qpc,after_setevent_qpc,completion_qpc,ok,frequency\n");
        }
    }
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(task_control),name.c_str());
    if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS) { error="CreateFileMappingW failed"; terminate(); return false; }
    control=static_cast<task_control*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(task_control)));
    if(!control) { error="MapViewOfFile failed"; terminate(); return false; }
    new(control) task_control{};
    if(attachment) control->attachment=*attachment;
    const auto cmd_name=name+L"_command", done_name=name+L"_completion";
    command_event=CreateEventW(nullptr,FALSE,FALSE,cmd_name.c_str());
    completion_event=CreateEventW(nullptr,FALSE,FALSE,done_name.c_str());
    if(!command_event || !completion_event) { error="CreateEventW failed"; terminate(); return false; }
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION info{};
    std::wstring cmd=L"\""+executable+L"\" --serverengine-task-shm \""+name+L"\"";
    std::vector<wchar_t> args(cmd.begin(),cmd.end()); args.push_back(L'\0');
    if(!CreateProcessW(executable.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)) {
        error="CreateProcessW failed: "+std::to_string(GetLastError()); terminate(); return false;
    }
    child=info.hProcess; thread=info.hThread;
    if(!exchange(message{magic,version,command::init},command::ready,error)) { terminate(); return false; }
    initialized=true; return true;
}
bool process::exchange(message request,command expected,std::string& error) {
    if(!child || !control) { error="Task process not started"; return false; }
    const LONG current=++sequence;
    const auto trace_start=perf07::qpc();
    control->request=request;
    InterlockedExchange(&control->request_sequence,current);
    if(!SetEvent(command_event)) { error="Task command event failed"; return false; }
    HANDLE handles[2]{completion_event,child};
    const ULONGLONG deadline=GetTickCount64()+timeout;
    while(true) {
        const ULONGLONG now=GetTickCount64();
        const DWORD remaining=now>=deadline?0:static_cast<DWORD>(deadline-now);
        const DWORD wait=WaitForMultipleObjects(2,handles,FALSE,remaining);
        if(wait==WAIT_OBJECT_0+1) { error="Task exited before reply"; return false; }
        if(wait==WAIT_TIMEOUT) { error="Task reply timeout"; return false; }
        if(wait!=WAIT_OBJECT_0) { error="Task completion wait failed"; return false; }
        if(InterlockedCompareExchange(&control->response_sequence,0,0)!=current) continue;
        const auto trace_end=perf07::qpc();
        const message response=control->response;
        if(request.action==command::run) perf_trace.write(current,request.cycle,trace_start,trace_end,0,0,
            response.signature==magic && response.protocol_version==version && response.action==expected && response.status==0);
        if(response.signature!=magic || response.protocol_version!=version || response.action!=expected ||
           response.cycle!=request.cycle || response.status!=0) {
            error="Invalid Task reply, cycle or status"; return false;
        }
        return true;
    }
}
bool process::begin_run(std::uint64_t cycle, std::uint32_t calls, std::string& error) {
    if (!initialized || !calls || in_flight || !child || !control) {
        error = "Task not initialized, calls zero, or request already in flight";
        return false;
    }
    in_flight_sequence = ++sequence;
    in_flight_cycle = cycle;
    in_flight_start = perf07::qpc();
    control->request = message{magic, version, command::run, 0, cycle, calls};
    InterlockedExchange(&control->request_sequence, in_flight_sequence);
    in_flight = true;
    dispatch_before = perf07::qpc();
    const BOOL signaled=SetEvent(command_event);
    dispatch_after = perf07::qpc();
    if (!signaled) {
        in_flight = false;
        error = "Task command event failed";
        return false;
    }
    return true;
}

bool process::complete_run(std::string& error) {
    if (!in_flight || !control) {
        error = "No outstanding Task request";
        return false;
    }
    const LONG response_sequence = InterlockedCompareExchange(&control->response_sequence, 0, 0);
    if (response_sequence != in_flight_sequence) {
        error = "Stale Task completion sequence";
        return false;
    }
    const auto response = control->response;
    const bool ok = response.signature == magic && response.protocol_version == version &&
        response.action == command::done && response.cycle == in_flight_cycle && response.status == 0;
    const auto completion_qpc=perf07::qpc();
    perf_trace.write(in_flight_sequence, in_flight_cycle, in_flight_start, completion_qpc, 0, 0, ok);
    if (dispatch_file) {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        std::fprintf(dispatch_file,"%ld,%llu,%lld,%lld,%lld,%d,%lld\n",
            static_cast<long>(in_flight_sequence),static_cast<unsigned long long>(in_flight_cycle),
            static_cast<long long>(dispatch_before),static_cast<long long>(dispatch_after),
            static_cast<long long>(completion_qpc),ok?1:0,static_cast<long long>(frequency.QuadPart));
    }
    in_flight = false;
    if (!ok) error = "Invalid Task reply, cycle or status";
    return ok;
}

bool process::run(std::uint64_t cycle,std::uint32_t calls,std::string& error) {
    if(!initialized || !calls) { error="Task not initialized or calls zero"; return false; }
    if(!exchange(message{magic,version,command::run,0,cycle,calls},command::done,error)) {
        terminate(); return false;
    }
    return true;
}
bool process::stop(std::string& error) {
    if(!initialized) { error="Task not initialized"; return false; }
    const bool ok=exchange(message{magic,version,command::stop},command::stopped,error);
    if(ok && WaitForSingleObject(child,timeout)!=WAIT_OBJECT_0) {
        error="Task did not exit after STOP"; terminate(); return false;
    }
    terminate(); return ok;
}
}
