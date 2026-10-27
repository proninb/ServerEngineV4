#include "read_only_file_mapping.hpp"
#include "project/persistence/compiled_project.hpp"
#include "project/graph/graph.hpp"
#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace cw::server;
struct cpu_time { std::uint64_t user=0,kernel=0; };
cpu_time cpu() {
    FILETIME created{},exited{},kernel{},user{};
    if (!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)) throw 1;
    const auto value=[](FILETIME f) { return (static_cast<std::uint64_t>(f.dwHighDateTime)<<32)|f.dwLowDateTime; };
    return {value(user),value(kernel)};
}
std::uint64_t cycles() {
    ULONG64 value=0;
    if (!QueryThreadCycleTime(GetCurrentThread(),&value)) throw 2;
    return value;
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    read_only_file_mapping mapping;
    if(mapping.open(argv[1])!=read_only_file_mapping_result::success) return 3;
    compiled_project_view compiled;
    if(compiled.bind(mapping.bytes())!=compiled_project_image_result::success ||
        compiled.verify_contents()!=compiled_project_image_result::success) return 4;
    std::vector<link_record> inputs;
    for(std::size_t i=0;i<compiled.link_count();++i) {
        link_record record;
        const auto h=compiled.link_at(i);
        if(!compiled.link(h,record)) return 5;
        inputs.push_back(record);
    }
    if(inputs.size()!=367144) return 6;
    // No second full semantic Graph: only the tested public resolved-link
    // storage/index is populated. Inputs are resolved persisted endpoints.
    for(unsigned round=0;round<34;++round) {
        graph g;
        std::uint64_t checksum=0;
        const auto c0=cpu();const auto q0=cycles();
        const auto t0=std::chrono::steady_clock::now();
        for(const auto& entry:inputs) {
            link_handle h;
            bool replaced=false;
            if(!succeeded(g.rebind_resolved_link(entry.source,entry.target,h,replaced)) || replaced) return 7;
            checksum+=h.value();
        }
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
        const auto q1=cycles();const auto c1=cpu();
        if(g.link_count()!=inputs.size()) return 8;
        for(std::size_t i=0;i<inputs.size();++i)
            if(g.link_at_target(inputs[i].target).value()!=i+1) return 9;
        const auto expected=static_cast<std::uint64_t>(inputs.size())*(inputs.size()+1)/2;
        if(checksum!=expected) return 10;
        std::cout<<"round="<<round<<",warmup="<<(round<2)<<",links="<<inputs.size()
                 <<",wall_ms="<<elapsed<<",user_ms="<<double(c1.user-c0.user)/10000
                 <<",kernel_ms="<<double(c1.kernel-c0.kernel)/10000
                 <<",thread_cycles="<<(q1-q0)<<",checksum="<<checksum<<'\n';
        // Destruction, file access and exhaustive result checks are outside timing.
    }
}
