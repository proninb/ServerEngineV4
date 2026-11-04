#include "task_host.hpp"
#include <Windows.h>
namespace {
unsigned calls=0;
bool init(){ calls=0; return true; }
bool main_call(){ ++calls; return true; }
void done(){}
}
int main(){ return cw::server::task_runtime::run_host({init,main_call,done}); }
