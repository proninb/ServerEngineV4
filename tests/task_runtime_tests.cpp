#include "task_process.hpp"
#include <Windows.h>
#include <string>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 1;
    cw::server::task_runtime::process task;
    std::string error;
    if(!task.start(argv[1],5000,error)) return 2;
    if(!task.run(1,3,error)) return 3;
    if(!task.run(6,1,error)) return 4;
    if(!task.stop(error)) return 5;
    return 0;
}
