#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <process.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#pragma pack(push,1)
struct Event{uint8_t op,owner;uint16_t reserved;uint32_t worker;uint64_t id,size,heap,address,ticks;};
#pragma pack(pop)
static_assert(sizeof(Event)==48);
struct Slot{void* pointer;uint8_t owner,heapIndex;};
struct Worker{HANDLE request,done,thread;bool stop;Event* batch;unsigned count;void* stackBase;};
static Slot* slots;static Worker* workers;static HANDLE heaps[64];static uint64_t sources[64];static unsigned heapCount;
static uint64_t allocations,frees,unmatched,live;static bool affinity;
static void fail(const char* s){fprintf(stderr,"FAIL %s win32=%lu\n",s,GetLastError());ExitProcess(2);}
static uint64_t memory(){PROCESS_MEMORY_COUNTERS_EX p{};if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)))fail("memory");return p.PrivateUsage;}
static unsigned heapIndex(uint64_t source){unsigned i=0;for(;i<heapCount&&sources[i]!=source;++i){}if(i==heapCount){if(i==64)fail("heap map");sources[i]=source;heaps[i]=i?HeapCreate(0,0,0):GetProcessHeap();ULONG lfh=2;if(!heaps[i]||!HeapSetInformation(heaps[i],HeapCompatibilityInformation,&lfh,sizeof(lfh)))fail("heap/LFH");++heapCount;}return i;}
static void execute(Event* batch,unsigned count){for(unsigned i=0;i<count;++i){const auto e=batch[i];if(e.id>9536484)fail("id bound");if(e.op==1){if(slots[e.id].pointer)fail("duplicate alloc");unsigned hi=heapIndex(e.heap);void* p=HeapAlloc(heaps[hi],0,e.size);if(!p)fail("HeapAlloc");slots[e.id]={p,e.owner,static_cast<uint8_t>(hi)};++allocations;++live;}else if(e.op==2){auto& s=slots[e.id];if(!s.pointer||s.owner!=e.owner)fail("free identity");if(!HeapFree(heaps[s.heapIndex],0,s.pointer))fail("HeapFree");s={};++frees;--live;}else if(e.op==4)++unmatched;else fail("batch op");}}
static unsigned __stdcall workerMain(void* value){auto w=static_cast<Worker*>(value);int local;MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(&local,&info,sizeof(info)))fail("stack query");w->stackBase=info.AllocationBase;SetEvent(w->done);while(true){if(WaitForSingleObject(w->request,INFINITE)!=WAIT_OBJECT_0)fail("request wait");if(w->stop)return 0;execute(w->batch,w->count);SetEvent(w->done);}}
static void start(unsigned id){if(!id||id>=10000||workers[id].thread)fail("worker start");auto& w=workers[id];w.request=CreateEventW(nullptr,FALSE,FALSE,nullptr);w.done=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!w.request||!w.done)fail("events");w.thread=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,workerMain,&w,0,nullptr));if(!w.thread||WaitForSingleObject(w.done,30000)!=WAIT_OBJECT_0)fail("beginthreadex");}
static void stop(unsigned id){auto& w=workers[id];if(!w.thread)fail("worker stop");w.stop=true;SetEvent(w.request);if(WaitForSingleObject(w.thread,30000)!=WAIT_OBJECT_0)fail("thread join");DWORD code=0;if(!GetExitCodeThread(w.thread,&code)||code)fail("worker exit");CloseHandle(w.thread);CloseHandle(w.request);CloseHandle(w.done);w={};}
static uint64_t regionCommit(void* base){uint64_t sum=0;auto address=static_cast<unsigned char*>(base);MEMORY_BASIC_INFORMATION m{};while(VirtualQuery(address,&m,sizeof(m))&&m.AllocationBase==base){if(m.State==MEM_COMMIT)sum+=m.RegionSize;address+=m.RegionSize;}return sum;}
static uint64_t stackCommit(){uint64_t sum=0;for(unsigned i=1;i<10000;++i)if(workers[i].thread)sum+=regionCommit(workers[i].stackBase);return sum;}
int main(int argc,char** argv){
 if(argc!=4)fail("usage replay strict.bin stages.bin single|thread-affinity");affinity=strcmp(argv[3],"thread-affinity")==0;if(!affinity&&strcmp(argv[3],"single"))fail("mode");
 constexpr uint64_t metadataBytes=9536485ULL*sizeof(Slot);slots=static_cast<Slot*>(VirtualAlloc(nullptr,metadataBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));workers=static_cast<Worker*>(VirtualAlloc(nullptr,10000*sizeof(Worker),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!slots||!workers)fail("metadata");memset(slots,0,metadataBytes);memset(workers,0,10000*sizeof(Worker));
 FILE* sf=nullptr;fopen_s(&sf,argv[2],"rb");if(!sf)fail("stages");unsigned char stages[4096];size_t stageCount=fread(stages,1,sizeof(stages),sf);fclose(sf);
 HANDLE file=CreateFileA(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);if(file==INVALID_HANDLE_VALUE)fail("input");ULONG lfh=2;if(!HeapSetInformation(GetProcessHeap(),HeapCompatibilityInformation,&lfh,sizeof(lfh)))fail("LFH");
 uint64_t initial=memory();printf("{\"stage\":\"initial\",\"private_bytes\":%llu,\"metadata_bytes\":%llu}\n",initial,metadataBytes+10000*sizeof(Worker));
 Event batch[1024];unsigned queued=0,currentWorker=0,active=0,created=0,stopped=0;
 auto flush=[&](){if(!queued)return;if(affinity){auto& w=workers[currentWorker];if(!w.thread)fail("task on inactive worker");w.batch=batch;w.count=queued;SetEvent(w.request);if(WaitForSingleObject(w.done,30000)!=WAIT_OBJECT_0)fail("batch timeout");}else execute(batch,queued);queued=0;};
 Event input[1024];DWORD bytes;
 while(true){if(!ReadFile(file,input,sizeof(input),&bytes,nullptr))fail("read");if(!bytes)break;if(bytes%sizeof(Event))fail("record tail");for(unsigned i=0;i<bytes/sizeof(Event);++i){Event e=input[i];
  if(e.op==1||e.op==2||e.op==4){if(queued&&(currentWorker!=e.worker||queued==1024))flush();currentWorker=e.worker;batch[queued++]=e;continue;}
  flush();
  if(e.op==5){if(affinity)start(e.worker);++active;++created;}
  else if(e.op==6){if(affinity)stop(e.worker);if(!active)fail("active underflow");--active;++stopped;}
  else if(e.op==3){if(e.id>=stageCount)fail("boundary");uint64_t privateBytes=memory(),stacks=stackCommit();DWORD handles=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handles))fail("handles");printf("{\"boundary\":%llu,\"stage_code\":%u,\"private_bytes\":%llu,\"drift_bytes\":%lld,\"worker_stack_commit\":%llu,\"private_minus_worker_stacks\":%llu,\"active_workers\":%u,\"handles\":%lu,\"live\":%llu}\n",e.id,stages[e.id],privateBytes,static_cast<long long>(privateBytes-initial),stacks,privateBytes-stacks,active,handles,live);fflush(stdout);}
  else fail("op");
 }}flush();if(active)fail("active workers at end");printf("{\"stage\":\"complete\",\"alloc\":%llu,\"free\":%llu,\"unmatched\":%llu,\"live\":%llu,\"workers_created\":%u,\"workers_stopped\":%u}\n",allocations,frees,unmatched,live,created,stopped);CloseHandle(file);VirtualFree(slots,0,MEM_RELEASE);VirtualFree(workers,0,MEM_RELEASE);return 0;
}
