import pathlib, sys, shutil
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
src=pathlib.Path(__file__).resolve().parent
runtime=root/'server_engine/project/task/runtime'
for filename in ('task_process.cpp','task_process.hpp','task_host.cpp'):
    if not (runtime/filename).exists(): raise SystemExit(f'Missing {runtime/filename}')
shutil.copy2(src/'task_control_v2.hpp',runtime/'task_control_v2.hpp')
def edit(name, old, new):
    p=runtime/name; s=p.read_text(encoding='utf-8')
    if new in s: return
    if old not in s: raise SystemExit(f'Anchor not found: {name}: {old[:70]}')
    p.write_text(s.replace(old,new,1),encoding='utf-8')
edit('task_process.hpp','#include "task_protocol.hpp"','#include "task_protocol.hpp"\n#include "task_control_v2.hpp"')
edit('task_process.hpp','    HANDLE child = nullptr;', '''    bool start_v2(const std::wstring& executable, std::uint32_t timeout_ms,
        const shm_attachment* attachment, std::string& error);
    bool exchange_v2(control_v2_command action, std::uint64_t cycle,
        std::uint32_t calls, std::string& error);
    void close_v2() noexcept;
    HANDLE mapping_v2 = nullptr;
    HANDLE attachment_mapping_v2 = nullptr;
    HANDLE command_event_v2 = nullptr;
    HANDLE done_event_v2 = nullptr;
    control_v2* control = nullptr;
    bool v2 = false;
    HANDLE child = nullptr;''')
edit('task_process.cpp','#include "task_process.hpp"','#include "task_process.hpp"\n#include <atomic>\n#include <cwchar>\n#include <new>')
edit('task_process.cpp','process::~process(){ terminate(); }','''void process::close_v2() noexcept {
    if (control) UnmapViewOfFile(control);
    control = nullptr;
    close_handle(command_event_v2);
    close_handle(done_event_v2);
    close_handle(mapping_v2);
    close_handle(attachment_mapping_v2);
    v2 = false;
}
process::~process(){ terminate(); }''')
edit('task_process.cpp','    close_handle(input); close_handle(output); close_handle(thread); close_handle(child);','    close_handle(input); close_handle(output); close_handle(thread); close_handle(child);\n    close_v2();')
edit('task_process.cpp','    terminate(); timeout=timeout_ms;','''    terminate(); timeout=timeout_ms;
    wchar_t mode[4]{};
    if (GetEnvironmentVariableW(L"TASK_RUNTIME_V2", mode, 4)==1 && mode[0]==L'1')
        return start_v2(executable,timeout_ms,attachment,error);''')
edit('task_process.cpp','bool process::exchange(message request,command expected,std::string& error) {','''bool process::start_v2(const std::wstring& executable, std::uint32_t timeout_ms,
    const shm_attachment* attachment, std::string& error) {
    static std::atomic<unsigned long> serial{0};
    const auto token = std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(++serial);
    const auto base = L"Local\\\\SEV4_TASK_V2_" + token;
    mapping_v2 = CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
        sizeof(control_v2),(base+L"_map").c_str());
    if (!mapping_v2 || GetLastError()==ERROR_ALREADY_EXISTS) {
        error="V2 mapping creation failed"; terminate(); return false;
    }
    control=static_cast<control_v2*>(MapViewOfFile(mapping_v2,FILE_MAP_ALL_ACCESS,0,0,sizeof(control_v2)));
    command_event_v2=CreateEventW(nullptr,FALSE,FALSE,(base+L"_cmd").c_str());
    done_event_v2=CreateEventW(nullptr,FALSE,FALSE,(base+L"_done").c_str());
    if (!control || !command_event_v2 || !done_event_v2) {
        error="V2 control objects creation failed"; terminate(); return false;
    }
    new (control) control_v2{};
    if (attachment) {
        if (!attachment->name[0] || attachment->name[128]!=0 || !attachment->size || !attachment->address) {
            error="Invalid V2 Project SHM attachment"; terminate(); return false;
        }
        attachment_mapping_v2=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
            sizeof(shm_attachment),(base+L"_attach").c_str());
        if (!attachment_mapping_v2) { error="V2 attachment mapping failed"; terminate(); return false; }
        auto* view=MapViewOfFile(attachment_mapping_v2,FILE_MAP_ALL_ACCESS,0,0,sizeof(shm_attachment));
        if (!view) { error="V2 attachment view failed"; terminate(); return false; }
        std::memcpy(view,attachment,sizeof(*attachment));
        UnmapViewOfFile(view);
    }
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION info{};
    // No pipes in V2: all control traffic uses mapping + events.
    std::wstring cmd=L"\\\""+executable+L"\\\" --serverengine-task-host --serverengine-task-v2 " + base;
    std::vector<wchar_t> args(cmd.begin(),cmd.end()); args.push_back(L'\\0');
    if (!CreateProcessW(executable.c_str(),args.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)) {
        error="V2 CreateProcessW failed: "+std::to_string(GetLastError()); terminate(); return false;
    }
    child=info.hProcess; thread=info.hThread;
    v2=true;
    if (!exchange_v2(control_v2_command::init,0,0,error)) { terminate(); return false; }
    initialized=true; return true;
}
bool process::exchange_v2(control_v2_command action,std::uint64_t cycle,
    std::uint32_t calls,std::string& error) {
    if (!v2 || !child || !control) { error="V2 process not ready"; return false; }
    const LONG seq=control->sequence+1;
    control->cycle=cycle;
    control->calls=calls;
    control->status=0;
    InterlockedExchange(&control->command,static_cast<LONG>(action));
    InterlockedExchange(&control->sequence,seq);
    if (!SetEvent(command_event_v2)) { error="V2 SetEvent failed"; return false; }
    HANDLE waits[]{done_event_v2,child};
    const DWORD result=WaitForMultipleObjects(2,waits,FALSE,timeout);
    if (result==WAIT_OBJECT_0+1) { error="V2 Task exited"; return false; }
    if (result==WAIT_TIMEOUT) { error="V2 Task timeout"; return false; }
    if (result!=WAIT_OBJECT_0) { error="V2 wait failed"; return false; }
    if (control->completed!=seq || control->status!=0) {
        error="V2 Task failed or sequence mismatch: status="+std::to_string(control->status);
        return false;
    }
    return true;
}
bool process::exchange(message request,command expected,std::string& error) {''')
edit('task_process.cpp','    if(!exchange(message{magic,version,command::run,0,cycle,calls},command::done,error))','    if(!(v2 ? exchange_v2(control_v2_command::run,cycle,calls,error) :\n        exchange(message{magic,version,command::run,0,cycle,calls},command::done,error)))')
edit('task_process.cpp','    const bool ok=exchange(message{magic,version,command::stop},command::stopped,error);','    const bool ok=v2 ? exchange_v2(control_v2_command::stop,0,0,error) :\n        exchange(message{magic,version,command::stop},command::stopped,error);')
edit('task_host.cpp','#include "task_protocol.hpp"','#include "task_protocol.hpp"\n#include "task_control_v2.hpp"\n#include <string>')
edit('task_host.cpp','int run_host(callbacks cb) {','''int run_host_v2(callbacks cb, const std::wstring& base) {
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(base+L"_map").c_str());
    HANDLE command_event=OpenEventW(SYNCHRONIZE,FALSE,(base+L"_cmd").c_str());
    HANDLE done_event=OpenEventW(EVENT_MODIFY_STATE,FALSE,(base+L"_done").c_str());
    if (!mapping || !command_event || !done_event) return 20;
    HANDLE attachment_mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,(base+L"_attach").c_str());
    if (attachment_mapping) {
        const auto* attachment=static_cast<const shm_attachment*>(
            MapViewOfFile(attachment_mapping,FILE_MAP_READ,0,0,sizeof(shm_attachment)));
        if (!attachment || !attachment->name[0] || attachment->name[128]!=0 ||
            !std::memchr(attachment->name,'\0',sizeof(attachment->name)) ||
            attachment->size>static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) ||
            attachment->address>static_cast<std::uint64_t>((std::numeric_limits<std::uintptr_t>::max)()) ||
            attached_mapping.open(attachment->name,static_cast<std::size_t>(attachment->size),
                static_cast<std::uintptr_t>(attachment->address))!=cw::server::fixed_shared_memory_result::success)
            return 24;
        UnmapViewOfFile(attachment);
        CloseHandle(attachment_mapping);
    }
    auto* control=static_cast<control_v2*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(control_v2)));
    if (!control || control->magic!=control_v2_magic || control->version!=2) return 21;
    LONG previous=0;
    for (;;) {
        if (WaitForSingleObject(command_event,INFINITE)!=WAIT_OBJECT_0) return 22;
        const LONG sequence=control->sequence;
        if (sequence<=previous) return 23;
        previous=sequence;
        const auto action=static_cast<control_v2_command>(control->command);
        bool ok=true;
        if (action==control_v2_command::init) {
            try { ok=cb.init(); } catch (...) { ok=false; }
        } else if (action==control_v2_command::run && control->calls) {
            try { for (std::uint32_t i=0;i<control->calls;++i)
                if (!cb.main()) { ok=false; break; } } catch (...) { ok=false; }
        } else if (action==control_v2_command::stop) {
            try { cb.done(); } catch (...) { ok=false; }
        } else ok=false;
        control->status=ok ? 0 : 1;
        InterlockedExchange(&control->completed,sequence);
        SetEvent(done_event);
        if (!ok || action==control_v2_command::stop) break;
    }
    UnmapViewOfFile(control);
    CloseHandle(mapping); CloseHandle(command_event); CloseHandle(done_event);
    return 0;
}
int run_host(callbacks cb) {
    const std::wstring cmdline=GetCommandLineW();
    const std::wstring marker=L"--serverengine-task-v2 ";
    const auto pos=cmdline.find(marker);
    if (pos!=std::wstring::npos) {
        const auto start=pos+marker.size();
        const auto end=cmdline.find_first_of(L" \\t\\r\\n",start);
        return run_host_v2(cb,cmdline.substr(start,end-start));
    }''')
print('Applied opt-in V2 control candidate with Project SHM attachment bootstrap.')
