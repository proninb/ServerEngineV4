#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// One thread reproduces trace event order, not original concurrent heap scheduling.
#pragma pack(push,1)
struct Event { uint8_t op,owner; uint16_t reserved; uint32_t tid; uint64_t id,size,heap,address,ticks; };
#pragma pack(pop)
static_assert(sizeof(Event)==48);
struct Slot { void* pointer; uint8_t owner,heapIndex; };
static void fail(const char* why){std::fprintf(stderr,"FAIL %s win32=%lu\n",why,GetLastError());std::exit(2);}
static uint64_t memory(){PROCESS_MEMORY_COUNTERS_EX p{};if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)))fail("memory");return p.PrivateUsage;}
int main(int argc,char** argv){
 if(argc!=5)fail("usage: replay events.bin stages.bin max_allocations shared|private");
 bool isolated=std::strcmp(argv[4],"private")==0;
 if(!isolated&&std::strcmp(argv[4],"shared")!=0)fail("arm");
 uint64_t max=std::strtoull(argv[3],nullptr,10)+1;
 auto slots=static_cast<Slot*>(VirtualAlloc(nullptr,max*sizeof(Slot),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 if(!slots)fail("metadata");std::memset(slots,0,max*sizeof(Slot));
 unsigned char stages[4096]{};
 FILE* st=nullptr;fopen_s(&st,argv[2],"rb");if(!st)fail("stages");size_t count=fread(stages,1,sizeof(stages),st);fclose(st);
 HANDLE input=CreateFileA(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);if(input==INVALID_HANDLE_VALUE)fail("input");
 HANDLE shared=GetProcessHeap(),domain=nullptr;ULONG lfh=2;if(!HeapSetInformation(shared,HeapCompatibilityInformation,&lfh,sizeof(lfh)))fail("LFH shared");
 uint64_t liveConstruction=0,live=0,alloc=0,freed=0,unmatched=0,destroyed=0;
 uint64_t sourceHeaps[64]{};HANDLE heaps[64]{};unsigned heapCount=0;
 const uint64_t initial=memory();
 std::printf("{\"stage\":\"initial\",\"private_bytes\":%llu,\"metadata_bytes\":%llu}\n",initial,max*sizeof(Slot));
 Event buffer[1024];DWORD got=0;
 while(true){if(!ReadFile(input,buffer,sizeof(buffer),&got,nullptr))fail("read");if(!got)break;if(got%sizeof(Event))fail("truncated record");
  for(size_t i=0;i<got/sizeof(Event);++i){auto e=buffer[i];
   if(e.op==1){
    unsigned hi=0;for(;hi<heapCount&&sourceHeaps[hi]!=e.heap;++hi){}
    if(hi==heapCount){if(heapCount==64)fail("heap map bound");sourceHeaps[hi]=e.heap;heaps[hi]=hi==0?shared:HeapCreate(0,0,0);if(!heaps[hi])fail("source HeapCreate");if(!HeapSetInformation(heaps[hi],HeapCompatibilityInformation,&lfh,sizeof(lfh)))fail("source LFH");++heapCount;}
    if(e.id>=max||!e.id||slots[e.id].pointer||e.owner>3)fail("allocation identity");
    HANDLE h=heaps[hi];
    if(isolated&&e.owner==1){if(!domain){domain=HeapCreate(0,0,0);if(!domain)fail("HeapCreate");if(!HeapSetInformation(domain,HeapCompatibilityInformation,&lfh,sizeof(lfh)))fail("LFH private");}h=domain;}
    void* p=HeapAlloc(h,0,static_cast<SIZE_T>(e.size));if(!p)fail("HeapAlloc");slots[e.id]={p,e.owner,static_cast<uint8_t>(hi)};
    ++alloc;++live;if(e.owner==1)++liveConstruction;
   }else if(e.op==2){
    if(e.id>=max||!slots[e.id].pointer||slots[e.id].owner!=e.owner)fail("free identity");
    if(!HeapFree(isolated&&e.owner==1?domain:heaps[slots[e.id].heapIndex],0,slots[e.id].pointer))fail("HeapFree");
    slots[e.id]={};++freed;--live;if(e.owner==1)--liveConstruction;
   }else if(e.op==3){
    if(e.id>=count)fail("boundary id");uint64_t before=memory();
    if(stages[e.id]==1){if(liveConstruction)fail("live Construction at P_after");if(isolated&&domain){if(!HeapDestroy(domain))fail("HeapDestroy");domain=nullptr;++destroyed;}}
    uint64_t after=memory();
    std::printf("{\"boundary\":%llu,\"stage_code\":%u,\"private_before\":%llu,\"private_after\":%llu,\"drift_bytes\":%lld,\"live\":%llu,\"construction_live\":%llu,\"destroyed\":%llu}\n",e.id,stages[e.id],before,after,static_cast<long long>(after-initial),live,liveConstruction,destroyed);
   }else if(e.op==4)++unmatched;else fail("operation");
  }
 }
 std::printf("{\"stage\":\"complete\",\"alloc\":%llu,\"free\":%llu,\"unmatched_free_excluded\":%llu,\"live\":%llu,\"construction_live\":%llu}\n",alloc,freed,unmatched,live,liveConstruction);
 CloseHandle(input);VirtualFree(slots,0,MEM_RELEASE);return 0;
}
