#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#pragma comment(lib,"psapi.lib")
namespace cw::server::heap_optimize14 {
inline void after_unload() noexcept {
    // Only the diagnostic source copy includes this header/call.
    char mode[5]{};const DWORD length=GetEnvironmentVariableA("SE_HEAP_OPT14",mode,sizeof(mode));
    const bool on=length==4 && std::strcmp(mode,"ON__")==0;
    const bool valid=length==0 || (length==4 && (on||std::strcmp(mode,"OFF_")==0));
    PROCESS_MEMORY_COUNTERS_EX before{},after{};before.cb=sizeof(before);after.cb=sizeof(after);
    const BOOL before_ok=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before));
    LARGE_INTEGER start{},end{},frequency{};const BOOL timer_ok=QueryPerformanceFrequency(&frequency)&&QueryPerformanceCounter(&start);
    BOOL success=FALSE;DWORD error=ERROR_SUCCESS;
    if(on){HEAP_OPTIMIZE_RESOURCES_INFORMATION info{};info.Version=HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION;info.Flags=0;
        success=HeapSetInformation(nullptr,HeapOptimizeResources,&info,sizeof(info));if(!success)error=GetLastError();}
    const BOOL end_ok=QueryPerformanceCounter(&end);
    const BOOL after_ok=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));
    static unsigned long long sequence=0;
    char row[768];const int size=std::snprintf(row,sizeof(row),"{\"unload\":%llu,\"on\":%s,\"selector_valid\":%s,\"called\":%s,\"success\":%s,\"error\":%lu,\"before_ok\":%s,\"after_ok\":%s,\"timer_ok\":%s,\"before_private\":%llu,\"after_private\":%llu,\"ticks\":%lld,\"frequency\":%lld}\n",++sequence,on?"true":"false",valid?"true":"false",on?"true":"false",success?"true":"false",error,before_ok?"true":"false",after_ok?"true":"false",timer_ok&&end_ok?"true":"false",static_cast<unsigned long long>(before.PrivateUsage),static_cast<unsigned long long>(after.PrivateUsage),end.QuadPart-start.QuadPart,frequency.QuadPart);
    // Identical logging in OFF/ON, outside the before/after API interval.
    HANDLE file=CreateFileW(L"heap-optimize.jsonl",FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD written=0;if(size>0&&size<static_cast<int>(sizeof(row)))(void)WriteFile(file,row,static_cast<DWORD>(size),&written,nullptr);CloseHandle(file);}
}
}
